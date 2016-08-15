#include "pb_conn.h"
#include "pink_define.h"
#include "worker_thread.h"
#include "xdebug.h"

#include <string>

namespace pink {

PbConn::PbConn(const int fd, const std::string &ip_port) :
  PinkConn(fd, ip_port),
  header_len_(-1),
  cur_pos_(0),
  rbuf_len_(0),
  connStatus_(kHeader),
  wbuf_len_(0),
  wbuf_pos_(0)
{
  rbuf_ = (char *)malloc(sizeof(char) * PB_MAX_MESSAGE);
  wbuf_ = (char *)malloc(sizeof(char) * PB_MAX_MESSAGE);
}

PbConn::~PbConn()
{
  free(rbuf_);
  free(wbuf_);
}

// Msg is [ length(COMMAND_HEADER_LENGTH) | body(length bytes) ]
//   step 1. kHeader, we read COMMAND_HEADER_LENGTH bytes;
//   step 2. kPacket, we read header_len bytes;
ReadStatus PbConn::GetRequest()
{
  bool flag = true;

  // TODO  cur_pos_ can be omitted
  while (flag) {
    switch (connStatus_) {
      case kHeader: {
        ssize_t nread = read(fd(), rbuf_ + rbuf_len_, COMMAND_HEADER_LENGTH - rbuf_len_);
        if (nread == -1) {
          if (errno == EAGAIN) {
            return kReadHalf;
          } else {
            return kReadError;
          }
        } else if (nread == 0) {
          return kReadClose;
        } else {
          rbuf_len_ += nread;
          if (rbuf_len_ - cur_pos_ >= COMMAND_HEADER_LENGTH) {
            uint32_t integer = 0;
            memcpy((char *)(&integer), rbuf_ + cur_pos_, sizeof(uint32_t));
            header_len_ = ntohl(integer);
            log_info("Header_len %u", header_len_);
            cur_pos_ += COMMAND_HEADER_LENGTH;
            connStatus_ = kPacket;
          }
          flag = false;
        }
        break;
      }
      case kPacket: {
        if (header_len_ >= PB_MAX_MESSAGE - COMMAND_HEADER_LENGTH) {
          return kFullError;
        } else {
          // read msg body
          ssize_t nread = read(fd(), rbuf_ + rbuf_len_, header_len_);
          if (nread == -1) {
            if (errno == EAGAIN) {
              return kReadHalf;
            } else {
              return kReadError;
            }
          } else if (nread == 0) {
            return kReadClose;
          }

          rbuf_len_ += nread;
          if (rbuf_len_ - cur_pos_ == header_len_) {
            cur_pos_ += header_len_;
            log_info("k Packet cur_pos_ %d rbuf_len_ %d", cur_pos_, rbuf_len_);
            connStatus_ = kComplete;
          }
        }
        break;
      }
      case kComplete: {
        DealMessage();
        connStatus_ = kHeader;
        log_info("%d %d", cur_pos_, rbuf_len_);

        cur_pos_ = 0;
        rbuf_len_ = 0;
        return kReadAll;
      }
      // Add this switch case just for delete compile warning
      case kBuildObuf:
        break;

      case kWriteObuf:
        break;
    }
  }

  return kReadHalf;
}

WriteStatus PbConn::SendReply()
{
  BuildObuf();
  ssize_t nwritten = 0;
  while (wbuf_len_ > 0) {
    nwritten = write(fd(), wbuf_ + wbuf_pos_, wbuf_len_ - wbuf_pos_);
    if (nwritten <= 0) {
      break;
    }
    wbuf_pos_ += nwritten;
    if (wbuf_pos_ == wbuf_len_) {
      wbuf_len_ = 0;
      wbuf_pos_ = 0;
    }
  }
  if (nwritten == -1) {
    if (errno == EAGAIN) {
      return kWriteHalf;
    } else {
      // Here we should close the connection
      return kWriteError;
    }
  }
  if (wbuf_len_ == 0) {
    return kWriteAll;
  } else {
    return kWriteHalf;
  }
}


Status PbConn::BuildObuf()
{
  wbuf_len_ = res_->ByteSize();
  res_->SerializeToArray(wbuf_ + 4, wbuf_len_);
  uint32_t u;
  u = htonl(wbuf_len_);
  memcpy(wbuf_, &u, sizeof(uint32_t));
  wbuf_len_ += COMMAND_HEADER_LENGTH;

  return Status::OK();
}

}
