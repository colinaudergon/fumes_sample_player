/**
 * @file ConsoleInputHandler.h
 * @brief IInputHandler implementation for native/Linux builds: reads typed commands from stdin
 * ("up" / "down" for now) and translates them into InputEvent, using CLI11 for parsing.
 *
 * This is the Linux stand-in for the physical navigation sources (buttons, ADC) used on RP2040
 * -- see hw_interfaces/include/CompositeInputHandler.h for how multiple IInputHandler sources
 * are meant to be aggregated once more than one exists on a given platform.
 */

#pragma once

#include <string>

#include "CLI11.hpp"
#include "IInputHandler.h"

namespace hw_interface
{

    class ConsoleInputHandler : public IInputHandler
    {
    public:
        ConsoleInputHandler();
        ~ConsoleInputHandler() override = default;

        int Init() override;

        /// @brief Non-blocking: returns false immediately if stdin has no complete line
        /// available yet, true and fills `out` once a recognized command has been read.
        bool PollEvent(InputEvent &out) override;

    private:
        /// @brief Extracts and removes the first complete "\n"-terminated line from
        /// read_buffer_, if any (after topping it up with whatever is currently available on
        /// stdin). Returns false if no full line is available yet.
        bool TakeNextLine(std::string &line);

        /// @brief Splits `line` on whitespace and parses the tokens against app_, filling `out`
        /// on a recognized command. Returns false for blank/unrecognized input.
        bool ParseLine(const std::string &line, InputEvent &out);

        // Bare words typed on stdin ("up", "down") are modeled as CLI11 subcommands rather than
        // flags: CLI11 flags require a "--" prefix (e.g. "--up") and reject positional use.
        CLI::App app_;
        CLI::App *up_command_ = nullptr;
        CLI::App *down_command_ = nullptr;

        // Owns any bytes read from stdin that don't yet form a complete line. Reading is done
        // manually via read() on an O_NONBLOCK stdin (set in Init()) instead of std::cin: mixing
        // poll()/select() with std::cin is unreliable because istream implementations are free
        // to over-read into their internal buffer -- e.g. libstdc++'s in_avail() conservatively
        // reports 0 for pipes/ttys even when bytes are already buffered, so a single stdin read
        // can silently satisfy several PollEvent() calls' worth of lines without poll() (or
        // in_avail()) ever reporting them as available again.
        std::string read_buffer_;
    };

} // namespace hw_interface
