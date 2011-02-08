/* Copyright 2010 Jukka Jyl�nki

   Licensed under the Apache License, Version 2.0 (the "License");
   you may not use this file except in compliance with the License.
   You may obtain a copy of the License at

       http://www.apache.org/licenses/LICENSE-2.0

   Unless required by applicable law or agreed to in writing, software
   distributed under the License is distributed on an "AS IS" BASIS,
   WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
   See the License for the specific language governing permissions and
   limitations under the License. */

/** @file UnixEvent.cpp
	@brief */

#include <cassert>

#include <sys/time.h>
#include <sys/types.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "kNet/Event.h"
#include "kNet/Types.h"
#include "kNet/NetworkLogging.h"

namespace kNet
{

Event::Event()
{
	fd[0] = -1;
	fd[1] = -1;
	type = EventWaitDummy;
}

Event::Event(int /*SOCKET*/ fd_, EventWaitType eventType)
:type(eventType)
{
	fd[0] = fd_;
	fd[1] = -1; // When creating an Event off a SOCKET, this Event is never Set manually, so leave the write descriptor null.
}

Event CreateNewEvent(EventWaitType type)
{
	Event e;
	e.Create(type);
	assert(e.IsValid());
	return e;
}

void Event::Create(EventWaitType type_)
{
	type = type_;

	if (pipe(fd) == -1)
	{
		LOG(LogError, "Error in Event::Create: %s(%d)!", strerror(errno), errno);
		return;
	}

	int ret = fcntl(fd[0], F_SETFL, O_NONBLOCK);
	if (ret == -1)
	{
		LOG(LogError, "Event::Create: fcntl failed to set fd[0] in nonblocking mode: %s(%d)", strerror(errno), errno);
		return;
	}
	ret = fcntl(fd[1], F_SETFL, O_NONBLOCK);
	if (ret == -1)
	{
		LOG(LogError, "Event::Create: fcntl failed to set fd[1] in nonblocking mode: %s(%d)", strerror(errno), errno);
		return;
	}

	///\todo Return success or failure.
}

void Event::Close()
{
	if (fd[0] != -1)
	{
		close(fd[0]);
		fd[0] = -1;
	}
	if (fd[1] != -1)
	{
		close(fd[1]);
		fd[1] = -1;
	}
}

bool Event::IsNull() const
{
	// An Event is null iff it is not readable. (There can be read-only Events which are not writable)
	return fd[0] == -1;
}

void Event::Reset()
{
	if (IsNull())
	{
		LOG(LogError, "Event::Reset() failed! Tried to reset an uninitialized Event!");
		return;
	}

	// Exhaust the pipe: read bytes off of it until there is nothing to read. This will cause select()ing on the
	// pipe to not trigger on read-availability. (The code in this class should maintain that the pipe never contains
	// more than one unread byte, but still better to loop here to be sure)
	u8 val = 0;
	int ret = 0;
	while(ret != -1)
	{
		ret = read(fd[0], &val, sizeof(val));
		if (ret == -1 && errno != EAGAIN)
			LOG(LogError, "Event::Reset() eventfd_read() failed: %s(%d)!", strerror(errno), (int)errno);
	}
}

void Event::Set()
{
	if (IsNull())
	{
		LOG(LogError, "Event::Set() failed! Tried to set an uninitialized Event!");
		return;
	}
	if (fd[1] == -1)
	{
		LOG(LogError, "Event::Set() failed! Tried to set a read-only Event! (This event is probably a Socket read descriptor");
		return;
	}

	// Read one byte off from the pipe. This will fail or succeed and we don't really care, the important thing 
	// is that Event::Set() will not increase the number of bytes in the pipe.
	u8 val = 1;
	read(fd[0], &val, sizeof(val));

	// Now that we removed one byte off the pipe (if there was one), we are allowed to add one in.
	val = 1; // It doesn't really matter what value we write here, but by convention we always write (and expect to read back) a single '1'.
	int ret = write(fd[1], &val, sizeof(val));
	if (ret == -1)
	{
		LOG(LogError, "Event::Set() write() failed: %s(%d)!", strerror(errno), (int)errno);
		return;
	}
}

bool Event::Test() const
{
	if (IsNull())
		return false;

	return Wait(0);
}

/// Returns true if the event was set during this time, or false if timout occurred.
bool Event::Wait(unsigned long msecs) const
{
	if (IsNull())
		return false;

	fd_set fds;
	timeval tv;
	tv.tv_sec = msecs / 1000;
	tv.tv_usec = (msecs - tv.tv_sec * 1000) * 1000;

	FD_ZERO(&fds);
	FD_SET(fd[0], &fds);
	// Wait on a read state.
	// "The file descriptor is readable if the counter has a value greater than 0."
	int ret = select(fd[0]+1, &fds, NULL, NULL, &tv); // http://linux.die.net/man/2/select
	if (ret == -1)
	{
		LOG(LogError, "Event::Wait: select() failed on an eventfd: %s(%d)!", strerror(errno), (int)errno);
		return false;
	}

	return ret != 0;
}

bool Event::IsValid() const
{
	return !IsNull();
}

} // ~kNet
