// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "QobuzClient.hxx"
#include "lib/crypto/MD5.hxx"

#include <fmt/core.h>

#include <cassert>
#include <stdexcept>

using std::string_view_literals::operator""sv;

namespace {

class QueryStringBuilder {
	bool first = true;

public:
	QueryStringBuilder &operator()(std::string &dest,
				       std::string_view name,
				       std::string_view value) noexcept {
		dest.push_back(first ? '?' : '&');
		first = false;

		dest += name;
		dest.push_back('=');
		dest += value; // TODO: escape

		return *this;
	}
};

} // namespace

QobuzClient::QobuzClient(EventLoop &event_loop,
			 const char *_base_url,
			 const char *_app_id, const char *_app_secret,
			 const char *_device_manufacturer_id,
			 const char *_username, const char *_email,
			 const char *_password,
			 const char *_format_id)
	:base_url(_base_url), app_id(_app_id), app_secret(_app_secret),
	 device_manufacturer_id(_device_manufacturer_id),
	 username(_username), email(_email), password(_password),
	 format_id(_format_id),
	 curl(event_loop),
	 defer_invoke_handlers(event_loop, BIND_THIS_METHOD(InvokeHandlers))
{
}

CurlGlobal &
QobuzClient::GetCurl() noexcept
{
	return *curl;
}

void
QobuzClient::StartLogin()
{
	assert(!session.IsDefined());
	assert(!login_request);
	assert(!handlers.empty());

	QobuzLoginHandler &handler = *this;
	login_request = std::make_unique<QobuzLoginRequest>(*curl, base_url,
							    app_id,
							    username, email,
							    password,
							    device_manufacturer_id,
							    handler);
	login_request->Start();
}

void
QobuzClient::AddLoginHandler(QobuzSessionHandler &h) noexcept
{
	const std::scoped_lock protect{mutex};
	assert(!h.is_linked());

	const bool was_empty = handlers.empty();
	handlers.push_front(h);

	if (!was_empty || login_request)
		return;

	if (session.IsDefined()) {
		ScheduleInvokeHandlers();
	} else {
		// TODO: throttle login attempts?

		try {
			StartLogin();
		} catch (...) {
			error = std::current_exception();
			ScheduleInvokeHandlers();
			return;
		}
	}
}

QobuzSession
QobuzClient::GetSession() const
{
	const std::scoped_lock protect{mutex};

	if (error)
		std::rethrow_exception(error);

	if (!session.IsDefined())
		throw std::runtime_error("No session");

	return session;
}

void
QobuzClient::OnQobuzLoginSuccess(QobuzSession &&_session) noexcept
{
	{
		const std::scoped_lock protect{mutex};
		session = std::move(_session);
		login_request.reset();
	}

	ScheduleInvokeHandlers();
}

void
QobuzClient::OnQobuzLoginError(std::exception_ptr _error) noexcept
{
	{
		const std::scoped_lock protect{mutex};
		error = std::move(_error);
		login_request.reset();
	}

	ScheduleInvokeHandlers();
}

void
QobuzClient::InvokeHandlers() noexcept
{
	const std::scoped_lock protect{mutex};
	while (!handlers.empty()) {
		auto &h = handlers.front();
		handlers.pop_front();

		const ScopeUnlock unlock(mutex);
		h.OnQobuzSession();
	}
}

std::string
QobuzClient::MakeUrl(const char *object, const char *method,
		     const Curl::Headers &query) const noexcept
{
	assert(!query.empty());

	auto uri = fmt::format("{}{}/{}"sv, base_url, object, method);

	QueryStringBuilder q;
	for (const auto &[key, url] : query)
		q(uri, key, url);

	q(uri, "app_id", app_id);
	return uri;
}

std::string
QobuzClient::MakeSignedUrl(const char *object, const char *method,
			   const Curl::Headers &query) const noexcept
{
	assert(!query.empty());

	auto uri = fmt::format("{}{}/{}"sv, base_url, object, method);

	QueryStringBuilder q;
	std::string concatenated_query(object);
	concatenated_query += method;
	for (const auto &[key, url] : query) {
		q(uri, key, url);

		concatenated_query += key;
		concatenated_query += url;
	}

	q(uri, "app_id", app_id);

	const auto request_ts = std::to_string(time(nullptr));
	q(uri, "request_ts", request_ts);
	concatenated_query += request_ts;

	concatenated_query += app_secret;

	const auto md5_hex = MD5Hex(std::as_bytes(std::span{concatenated_query}));
	q(uri, "request_sig", std::string_view{md5_hex.data(), md5_hex.size()});

	return uri;
}
