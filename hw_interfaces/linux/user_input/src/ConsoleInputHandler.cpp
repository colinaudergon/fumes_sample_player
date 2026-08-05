#include "ConsoleInputHandler.h"

#include <fcntl.h>
#include <unistd.h>

#include <cerrno>
#include <iostream>
#include <sstream>

hw_interface::ConsoleInputHandler::ConsoleInputHandler()
{
    up_command_ = app_.add_subcommand("up", "Navigate up");
    down_command_ = app_.add_subcommand("down", "Navigate down");
}

int hw_interface::ConsoleInputHandler::Init()
{
    // Switch stdin to non-blocking mode so read() below never stalls PollEvent() while waiting
    // for a line that hasn't been typed yet.
    const int flags = fcntl(STDIN_FILENO, F_GETFL, 0);
    if (flags == -1 || fcntl(STDIN_FILENO, F_SETFL, flags | O_NONBLOCK) == -1)
    {
        return -1;
    }

    std::cout << "Console input ready. Type 'up' or 'down' then press enter.\n";
    return 0;
}

bool hw_interface::ConsoleInputHandler::TakeNextLine(std::string &line)
{
    // Drain whatever is currently available on stdin into read_buffer_ (non-blocking: a
    // O_NONBLOCK read() returns immediately with EAGAIN/EWOULDBLOCK once the pipe/tty has
    // nothing left, instead of stalling).
    char chunk[256];
    for (;;)
    {
        const ssize_t bytes_read = read(STDIN_FILENO, chunk, sizeof(chunk));
        if (bytes_read > 0)
        {
            read_buffer_.append(chunk, static_cast<size_t>(bytes_read));
            continue;
        }
        break; // bytes_read == 0 (EOF) or < 0 (EAGAIN/EWOULDBLOCK/other): nothing more to drain.
    }

    const size_t newline_pos = read_buffer_.find('\n');
    if (newline_pos == std::string::npos)
    {
        return false;
    }

    line = read_buffer_.substr(0, newline_pos);
    read_buffer_.erase(0, newline_pos + 1);
    return true;
}

bool hw_interface::ConsoleInputHandler::ParseLine(const std::string &line, InputEvent &out)
{
    std::istringstream stream(line);
    std::vector<std::string> tokens{std::istream_iterator<std::string>(stream), std::istream_iterator<std::string>()};
    if (tokens.empty())
    {
        return false;
    }

    // CLI11 apps/subcommands retain "was this parsed" state across calls; clear it before
    // reparsing the next line so a stale match from a previous line can't leak through.
    app_.clear();
    try
    {
        // CLI11 expects arguments in reverse order (it pops from the back).
        app_.parse(std::vector<std::string>(tokens.rbegin(), tokens.rend()));
    }
    catch (const CLI::ParseError &)
    {
        return false; // unrecognized command: ignore and keep polling.
    }

    if (up_command_->parsed())
    {
        out.type = InputEventType::kNavigationEvent;
        out.navigationDirection = NavigationDirection::kUp;
        return true;
    }
    if (down_command_->parsed())
    {
        out.type = InputEventType::kNavigationEvent;
        out.navigationDirection = NavigationDirection::kDown;
        return true;
    }
    return false;
}

bool hw_interface::ConsoleInputHandler::PollEvent(InputEvent &out)
{
    std::string line;
    // Keep consuming buffered lines until a recognized command produces an event or no more
    // complete lines remain -- otherwise a single unrecognized/blank line would stall recognition
    // of a valid command queued right behind it.
    while (TakeNextLine(line))
    {
        if (ParseLine(line, out))
        {
            return true;
        }
    }
    return false;
}
