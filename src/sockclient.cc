//
// Created by jemyzhang on 16-11-14.
//

#include <cerrno>
#include <cstring>
#include <functional>
#include <iostream>
#include <sys/epoll.h>
#include <unistd.h>

#include "ioloop.h"
#include "sockclient.h"

namespace SerialOverSocket {

int Client::input_data_handler(int fd) {
  int done = 0;
  while (true) {
    ssize_t count;
    char buf[128];
    memset(buf, 0, sizeof(buf));

    count = read(fd, buf, sizeof buf);
    if (count == -1) {
      /* If errno == EAGAIN, that means we have read all
         data. So go back to the main loop. */
      if (errno != EAGAIN) {
        perror("read");
        done = 1;
      }
      break;
    } else if (count == 0) {
      /* End of file. The remote has closed the
         connection. */
      done = 1;
      break;
    } else {
      if (fd == fileno(stdin)) {
        write_txbuf(buf, count);
      } else {
        write_rxbuf(buf, count);
      }
    }
  }

  if (done) {
    cerr << "Server Connection Closed" << endl;
    exit(0);
  }

  return 0;
}

int Client::input_event_handler(int fd) {
  /* We have data on the fd_ waiting to be read. Read and
     display it. We must read whatever data is available
     completely, as we are running in edge-triggered mode
     and won't get a notification again for the same
     data. */
  input_data_handler(fd);
  return 0;
}

int Client::output_data_handler() {
  flush();

  return 0;
}

int Client::handle(epoll_event e) {
  if ((e.events & EPOLLERR) || (e.events & EPOLLHUP) ||
      (!(e.events & EPOLLIN) && !(e.events & EPOLLOUT))) {
    /* An error has occured on this fd, or the socket is not
       ready for reading (why were we notified then?) */
    cerr << "epoll error" << endl;
    IOLoop::getInstance().get()->removeHandler(e.data.fd);
    close(e.data.fd);
    exit(0);
  } else if (e.events & EPOLLIN) {
    input_event_handler(e.data.fd);
  } else if (e.events & EPOLLOUT) {
    output_data_handler();
  }
  return 0;
}

Client::Client(ClientConfig::Info &info)
    : Connection(0, {Connection::CONNECTION_CLIENT, "", ""}),
      client_socket_(info.server.address, info.server.port, true) {
  if (client_socket_.fileno() < 0) {
    cerr << "failed to connect to the server: " << info.server.address << ":"
         << info.server.port << endl;
    exit(1);
  }

  fd_ = client_socket_.fileno();
  client_socket_.setunblock();

  set_termio_mode();
  Socket::setunblock(fileno(stdin));

  IOLoop::getInstance()->addHandler(client_socket_.fileno(), this,
                                    EPOLLIN | EPOLLET);
  IOLoop::getInstance()->addHandler(fileno(stdin), this, EPOLLIN | EPOLLET);
}

Client::~Client() { tcsetattr(STDIN_FILENO, TCSAFLUSH, &term_option_); }

ssize_t Client::write_rxbuf(const char *content, ssize_t /* length */) {
  cout << content << std::flush;
  return 0;
}

void Client::set_termio_mode() {
  struct termios options {};
  if (tcgetattr(fileno(stdin), &options) != 0) {
    throw("failed to get tc attr");
  }

  term_option_ = options;

  cfmakeraw(&options);
  options.c_iflag |= (ICRNL);
  options.c_oflag |= (OPOST | ONLCR);

  options.c_lflag &= ~ECHO;
  options.c_lflag &= ~ICANON;
  options.c_cc[VMIN] = 1;
  options.c_cc[VTIME] = 1;
  tcsetattr(fileno(stdin), TCSAFLUSH, &options);
}
}