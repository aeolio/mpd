// SPDX-License-Identifier: GPL-2.0-or-later
// Copyright The Music Player Daemon Project

#include "event/Thread.hxx"
#include "storage/Registry.hxx"
#include "storage/StorageInterface.hxx"
#include "storage/FileInfo.hxx"
#include "net/Init.hxx"
#include "time/ChronoUtil.hxx"
#include "time/ISO8601.hxx"
#include "util/PrintException.hxx"
#include "util/StringAPI.hxx"
#include "util/StringBuffer.hxx"

#include <memory>
#include <stdexcept>

#include <unistd.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>

class GlobalInit {
	const ScopeNetInit net_init;
	EventThread io_thread;

public:
	GlobalInit() {
		io_thread.Start();
	}

	EventLoop &GetEventLoop() noexcept {
		return io_thread.GetEventLoop();
	}
};

static std::unique_ptr<Storage>
MakeStorage(EventLoop &event_loop, const char *uri)
{
	auto storage = CreateStorageURI(event_loop, uri);
	if (storage == nullptr)
		throw std::runtime_error("Unrecognized storage URI");

	return storage;
}

static int
Ls(Storage &storage, const char *path)
{
	auto dir = storage.OpenDirectory(path);

	const char *name;
	while ((name = dir->Read()) != nullptr) {
		const auto info = dir->GetInfo(false);

		const char *type = "unk";
		switch (info.type) {
		case StorageFileInfo::Type::OTHER:
			type = "oth";
			break;

		case StorageFileInfo::Type::REGULAR:
			type = "reg";
			break;

		case StorageFileInfo::Type::DIRECTORY:
			type = "dir";
			break;
		}

		StringBuffer<64> mtime_buffer;
		const char *mtime = "          ";
		if (!IsNegative(info.mtime)) {
			mtime_buffer = FormatISO8601(info.mtime);
			mtime = mtime_buffer;
		}

		printf("%s %10llu %s %s\n",
		       type, (unsigned long long)info.size,
		       mtime, name);
	}

	return EXIT_SUCCESS;
}

static int
Stat(Storage &storage, const char *path)
{
	const auto info = storage.GetInfo(path, false);
	switch (info.type) {
	case StorageFileInfo::Type::OTHER:
		printf("other\n");
		break;

	case StorageFileInfo::Type::REGULAR:
		printf("regular\n");
		break;

	case StorageFileInfo::Type::DIRECTORY:
		printf("directory\n");
		break;
	}

	printf("size: %llu\n", (unsigned long long)info.size);

	return EXIT_SUCCESS;
}

int
main(int argc, char **argv)
try {
	if (argc < 3) {
		fprintf(stderr, "Usage: run_storage COMMAND URI ...\n");
		return EXIT_FAILURE;
	}

	const char *const command = argv[1];
	const char *const storage_uri = argv[2];

	GlobalInit init;

	if (StringIsEqual(command, "ls")) {
		if (argc != 4) {
			fprintf(stderr, "Usage: run_storage ls URI PATH\n");
			return EXIT_FAILURE;
		}

		const char *const path = argv[3];

		auto storage = MakeStorage(init.GetEventLoop(),
					   storage_uri);

		return Ls(*storage, path);
	} else if (StringIsEqual(command, "stat")) {
		if (argc != 4) {
			fprintf(stderr, "Usage: run_storage stat URI PATH\n");
			return EXIT_FAILURE;
		}

		const char *const path = argv[3];

		auto storage = MakeStorage(init.GetEventLoop(),
					   storage_uri);

		return Stat(*storage, path);
	} else {
		fprintf(stderr, "Unknown command\n");
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
} catch (...) {
	PrintException(std::current_exception());
	return EXIT_FAILURE;
}
