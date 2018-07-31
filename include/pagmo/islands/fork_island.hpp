/* Copyright 2017-2018 PaGMO development team

This file is part of the PaGMO library.

The PaGMO library is free software; you can redistribute it and/or modify
it under the terms of either:

  * the GNU Lesser General Public License as published by the Free
    Software Foundation; either version 3 of the License, or (at your
    option) any later version.

or

  * the GNU General Public License as published by the Free Software
    Foundation; either version 3 of the License, or (at your option) any
    later version.

or both in parallel, as here.

The PaGMO library is distributed in the hope that it will be useful, but
WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License
for more details.

You should have received copies of the GNU General Public License and the
GNU Lesser General Public License along with the PaGMO library.  If not,
see https://www.gnu.org/licenses/. */

#ifndef PAGMO_FORK_ISLAND_HPP
#define PAGMO_FORK_ISLAND_HPP

#include <cassert>
#include <cerrno>
#include <csignal>
#include <cstddef>
#include <cstdlib>
#include <cstring>
#include <exception>
#include <ios>
#include <iostream>
#include <iterator>
#include <sstream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>

#include <sys/types.h>
#include <unistd.h>

#include <boost/numeric/conversion/cast.hpp>

#include <pagmo/algorithm.hpp>
#include <pagmo/exceptions.hpp>
#include <pagmo/island.hpp>
#include <pagmo/population.hpp>
#include <pagmo/serialization.hpp>

namespace pagmo
{

class fork_island
{
    static void raise_errno(const std::string &fname)
    {
        pagmo_throw(std::runtime_error, "The call to the function " + fname
                                            + "() in the fork island failed with error code " + std::to_string(errno)
                                            + " and the following error message: '" + std::strerror(errno) + "'");
    }
    // Small RAII wrapper around a pipe.
    struct pipe_t {
        // Def ctor: will create the pipe.
        pipe_t() : r_status(true), w_status(true)
        {
            int fd[2];
            if (pipe(fd) == -1) {
                raise_errno("pipe");
            }
            // The pipe was successfully opened, copy over
            // the r/w descriptors.
            read = fd[0];
            write = fd[1];
        }
        // Try to close the reading end if it has not been closed already.
        // This will throw on error.
        void close_r()
        {
            if (r_status) {
                if (close(read) == -1) {
                    raise_errno("close");
                }
                r_status = false;
            }
        }
        // Try to close the writing end if it has not been closed already.
        // This will throw on error.
        void close_w()
        {
            if (w_status) {
                if (close(write) == -1) {
                    raise_errno("close");
                }
                w_status = false;
            }
        }
        ~pipe_t()
        {
            // Attempt to close the pipe on destruction.
            try {
                close_r();
                close_w();
            } catch (...) {
                // We are in a dtor, the error is not recoverable.
                std::cerr
                    << "An unrecoverable error was raised in the destructor of a pipe in fork_island(). Exiting now."
                    << std::endl;
                std::exit(1);
            }
        }
        // The file descriptors of the two ends of the pipe.
        int read, write;
        // Flag to signal the status of the two ends
        // of the pipe: true for open, false for closed.
        bool r_status, w_status;
    };
    // The structure we use to pass messages from the child to the parent:
    // - int, status flag,
    // - string, error message,
    // - the algorithm used for evolution,
    // - the evolved population.
    using message_t = std::tuple<int, std::string, algorithm, population>;

public:
    std::string get_name() const
    {
        return "Fork island";
    }
    void run_evolve(island &isl) const
    {
        // A message that will be used both by parent and child.
        message_t m;
        // The pipe.
        pipe_t p;
        // Try to fork now.
        auto child_pid = fork();
        if (child_pid == -1) {
            // Forking failed.
            raise_errno("fork");
        }
        if (child_pid) {
            // We are in the parent.
            try {
                // Close the write descriptor, we don't need to send anything to the child.
                p.close_w();
                // Prepare a local buffer and a stringstream, then read the data from the child.
                char buffer[1024];
                std::stringstream ss;
                {
                    cereal::BinaryInputArchive iarchive(ss);
                    while (true) {
                        const auto read_bytes = read(p.read, static_cast<void *>(buffer), sizeof(buffer));
                        if (read_bytes == 0) {
                            // EOF, break out.
                            break;
                        }
                        if (read_bytes == -1) {
                            raise_errno("read");
                        }
                        ss.write(buffer, boost::numeric_cast<std::streamsize>(read_bytes));
                    }
                    iarchive(m);
                }
                // Close the read descriptor.
                p.close_r();
            } catch (...) {
                // Something failed above. Try to kill the child
                // before re-raising the error.
                if (kill(child_pid, SIGTERM) == -1 && errno != ESRCH) {
                    // The signal delivery to the child failed, and not because
                    // the child does not exist any more (if the child did not exist,
                    // errno would be ESRCH).
                    std::cerr << "An unrecoverable error was raised while handling another error in the parent process "
                                 "of fork_island(). Giving up now."
                              << std::endl;
                    std::exit(1);
                }
                // Re-raise.
                throw;
            }
            // At this point, we have received the data from the child, and we can insert
            // it into isl, or raise an error.
            if (std::get<0>(m)) {
                pagmo_throw(std::runtime_error, "The run_evolve() method of fork_island raised an error in the "
                                                "child process. The full error message reported by the child is:\n"
                                                    + std::get<1>(m));
            }
            isl.set_algorithm(std::move(std::get<2>(m)));
            isl.set_population(std::move(std::get<3>(m)));
        } else {
            // We are in the child.
            //
            // A small helper to send the serialized representation of
            // a message_t to the parent.
            auto send_message = [&p](const message_t &ms) {
                std::stringstream ss;
                {
                    cereal::BinaryOutputArchive oarchive(ss);
                    oarchive(ms);
                }
                char buffer[1024];
                while (!ss.eof()) {
                    // Copy a chunk of data from the stream to the local buffer.
                    ss.read(buffer, boost::numeric_cast<std::streamsize>(sizeof(buffer)));
                    // Figure out how much we actually read.
                    const auto read_bytes = boost::numeric_cast<std::size_t>(ss.gcount());
                    assert(read_bytes <= sizeof(buffer));
                    // Now let's send the current content of the buffer to the parent.
                    if (write(p.write, static_cast<const void *>(buffer), read_bytes) == -1) {
                        raise_errno("write");
                    }
                }
            };
            // Fatal error message.
            constexpr char fatal_msg[]
                = "An unrecoverable error was raised while handling another error in the child process "
                  "of fork_island(). Giving up now.";
            try {
                // Close the read descriptor, we don't need to read anything from the parent.
                p.close_r();
                // Run the evolution.
                auto algo = isl.get_algorithm();
                auto new_pop = algo.evolve(isl.get_population());
                // Pack in m and serialize the result of the evolution.
                // NOTE: m was def cted, which, for tuples, value-inits all members.
                // So the status flag is already zero and the error message empty.
                std::get<2>(m) = std::move(algo);
                std::get<3>(m) = std::move(new_pop);
                // Send the message.
                send_message(m);
                // Close the write descriptor.
                p.close_w();
                // All done, we can kill the child.
                std::exit(0);
            } catch (const std::exception &e) {
                // If we caught an std::exception, set the error message in
                // m before continuing.
                try {
                    std::get<1>(m) = e.what();
                } catch (...) {
                    std::cerr << fatal_msg << std::endl;
                    std::exit(1);
                }
            } catch (...) {
                // Not an std::exception, we won't have an error message.
            }
            // If we get here, it means that something went wrong above. We will try
            // to send an error message back to the parent, failing that we will bail out.
            // Set the error flag.
            std::get<0>(m) = 1;
            try {
                // Make sure the algo/pop in m are set to serializable entities.
                std::get<2>(m) = algorithm{};
                std::get<3>(m) = population{};
                // Send the message.
                send_message(m);
                // Close the write descriptor.
                p.close_w();
                // All done, we can kill the child.
                std::exit(0);
            } catch (...) {
                std::cerr << fatal_msg << std::endl;
                std::exit(1);
            }
        }
    }
    template <typename Archive>
    void serialize(Archive &)
    {
    }
};

} // namespace pagmo

PAGMO_REGISTER_ISLAND(pagmo::fork_island)

#endif
