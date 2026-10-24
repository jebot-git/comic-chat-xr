// SPDX-License-Identifier: MIT
#include "irc.h"
#include <atomic>
#include <chrono>
#include <deque>
#include <gio/gio.h>
#include <mutex>
#include <stdexcept>
#include <thread>
namespace comic {
struct Irc::State {
  std::mutex mutex;
  std::deque<std::string> outgoing;
  std::deque<Json> incoming;
  std::atomic_bool online{false};
  GCancellable *cancel = g_cancellable_new();
  ~State() { g_object_unref(cancel); }
  void event(Json j) {
    std::lock_guard lock(mutex);
    if (incoming.size() >= 512)
      incoming.pop_front();
    incoming.push_back(std::move(j));
  }
};
Irc::Irc() : state(std::make_shared<State>()) {}
Irc::~Irc() { disconnect(); }
void Irc::disconnect() {
  g_cancellable_cancel(state->cancel);
  state->online = false;
}
bool Irc::connected() const { return state->online; }
bool Irc::send(std::string line) {
  if (!connected() || line.size() > 510 ||
      line.find_first_of("\r\n\0", 0, 3) != std::string::npos)
    return false;
  std::lock_guard lock(state->mutex);
  if (state->outgoing.size() >= 128)
    return false;
  state->outgoing.push_back(std::move(line));
  return true;
}
std::vector<Json> Irc::receive() {
  std::lock_guard lock(state->mutex);
  std::vector<Json> out(state->incoming.begin(), state->incoming.end());
  state->incoming.clear();
  return out;
}
void Irc::connect(Connection c) {
  auto token = [](const std::string &s) {
    return !s.empty() &&
           s.find_first_of(" \r\n\0:,", 0, 6) == std::string::npos;
  };
  if (!token(c.host) || !token(c.nick) || !token(c.channel) ||
      c.nick.size() > 30 || c.host.size() > 253 || c.channel.size() > 100 ||
      c.port < 1 || c.port > 65535 ||
      c.password.find_first_of("\r\n\0", 0, 3) != std::string::npos ||
      c.password.size() > 300)
    throw std::runtime_error("Invalid IRC host, nickname, channel or password");
  disconnect();
  state = std::make_shared<State>();
  auto s = state;
  std::thread([s, c = std::move(c)]() mutable {
    GSocketClient *client = g_socket_client_new();
    GSocketConnection *conn = nullptr;
    GError *error = nullptr;
    auto failure = [&]() {
      std::string message = error ? error->message : "IRC transport failure";
      if (error) {
        g_error_free(error);
        error = nullptr;
      }
      throw std::runtime_error(message);
    };
    try {
      s->event(
          {{"type", "status"},
           {"text", "Connecting to " + c.host + (c.tls ? " with TLS" : "")}});
      g_socket_client_set_timeout(client, 15);
      g_socket_client_set_tls(client, c.tls);
      conn = g_socket_client_connect_to_host(client, c.host.c_str(), c.port,
                                             s->cancel, &error);
      if (!conn)
        failure();
      auto *in = g_io_stream_get_input_stream(G_IO_STREAM(conn));
      auto *out = g_io_stream_get_output_stream(G_IO_STREAM(conn));
      if (!G_IS_POLLABLE_INPUT_STREAM(in))
        throw std::runtime_error("IRC input stream cannot be polled");
      auto write = [&](const std::string &line) {
        std::string wire = line + "\r\n";
        gsize count = 0;
        if (!g_output_stream_write_all(out, wire.data(), wire.size(), &count,
                                       s->cancel, &error))
          failure();
      };
      if (!c.password.empty())
        write("PASS :" + c.password);
      std::fill(c.password.begin(), c.password.end(), '\0');
      write("NICK " + c.nick);
      write("USER " + c.nick + " 0 * :de-xr Comic Chat");
      s->online = true;
      s->event({{"type", "status"},
                {"text", c.tls ? "TLS connected; registering"
                               : "Connected; registering"}});
      std::string buffer;
      bool joined = false;
      auto last = std::chrono::steady_clock::now();
      while (!g_cancellable_is_cancelled(s->cancel)) {
        std::deque<std::string> queue;
        {
          std::lock_guard lock(s->mutex);
          queue.swap(s->outgoing);
        }
        for (auto &line : queue)
          write(line);
        char block[4096];
        auto n = g_pollable_input_stream_read_nonblocking(
            G_POLLABLE_INPUT_STREAM(in), block, sizeof(block), s->cancel,
            &error);
        if (n < 0) {
          if (g_error_matches(error, G_IO_ERROR, G_IO_ERROR_WOULD_BLOCK)) {
            g_clear_error(&error);
          } else
            failure();
        } else if (n == 0)
          throw std::runtime_error("Server closed the connection");
        else {
          last = std::chrono::steady_clock::now();
          buffer.append(block, n);
          size_t end;
          while ((end = buffer.find('\n')) != std::string::npos) {
            auto line = buffer.substr(0, end);
            buffer.erase(0, end + 1);
            if (!line.empty() && line.back() == '\r')
              line.pop_back();
            if (line.size() > 8192)
              throw std::runtime_error("IRC line too large");
            auto parsed = parseIrc(line);
            if (parsed.command == "PING" && !parsed.args.empty())
              write("PONG :" + parsed.args.back());
            if (parsed.command == "001" && !joined) {
              joined = true;
              write("JOIN " + c.channel);
            }
            s->event({{"type", "line"}, {"line", line}});
          }
          if (buffer.size() > 8192)
            throw std::runtime_error("IRC input buffer exceeded limit");
        }
        if (std::chrono::steady_clock::now() - last > std::chrono::minutes(6))
          throw std::runtime_error("IRC server timeout");
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
      }
    } catch (const std::exception &e) {
      if (!g_cancellable_is_cancelled(s->cancel))
        s->event({{"type", "status"},
                  {"text", std::string("Disconnected: ") + e.what()}});
    }
    s->online = false;
    if (conn) {
      g_io_stream_close(G_IO_STREAM(conn), nullptr, nullptr);
      g_object_unref(conn);
    }
    g_object_unref(client);
  }).detach();
}
} // namespace comic
