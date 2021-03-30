
#include <cstdio>

#include "statisticsinterface.h"
#include "uvgrtpreceiver.h"
#include "common.h"

#include <QDateTime>

#define RTP_HEADER_SIZE 2
#define FU_HEADER_SIZE  1

static void __receiveHook(void *arg, uvg_rtp::frame::rtp_frame *frame)
{
  if (arg && frame)
  {
    static_cast<UvgRTPReceiver *>(arg)->receiveHook(frame);
  }
}

UvgRTPReceiver::UvgRTPReceiver(uint32_t sessionID, QString id, StatisticsInterface *stats,
                               DataType type, QString media, QFuture<uvg_rtp::media_stream *> stream):
  Filter(id, "RTP Receiver " + media, stats, NONE, type),
  type_(type),
  addStartCodes_(true),
  sessionID_(sessionID)
{
  watcher_.setFuture(stream);

  connect(&watcher_, &QFutureWatcher<uvg_rtp::media_stream *>::finished,
          [this]()
          {
            if (!watcher_.result())
              emit zrtpFailure(sessionID_);
            else
              watcher_.result()->install_receive_hook(this, __receiveHook);
          });
}

UvgRTPReceiver::~UvgRTPReceiver()
{

}

void UvgRTPReceiver::process()
{

}


void UvgRTPReceiver::receiveHook(uvg_rtp::frame::rtp_frame *frame)
{
  Q_ASSERT(frame && frame->payload != nullptr);

  if (frame == nullptr ||
      frame->payload == nullptr ||
      frame->payload_len == 0)
  {
    printProgramError(this,  "Received a nullptr frame from uvgRTP");
    return;
  }

  if (addStartCodes_ && type_ == HEVCVIDEO)
  {
    frame->payload_len += 4;
  }

  Data *received_picture = new Data;
  received_picture->data_size = frame->payload_len;
  received_picture->type = type_;
  received_picture->data = std::unique_ptr<uchar[]>(new uchar[received_picture->data_size]);
  received_picture->width = 0; // not known at this point. Decoder tells the correct resolution
  received_picture->height = 0;
  received_picture->framerate = 0;
  received_picture->source = REMOTE;

  // TODO: Get this info from RTP
  received_picture->presentationTime = QDateTime::currentMSecsSinceEpoch();

  // TODO: This copying should be done in separate thread as in
  // framedsource if we want to receive 4K with less powerful thread (like in Xeon)
  if (addStartCodes_ && type_ == HEVCVIDEO)
  {
    memcpy(received_picture->data.get() + 4, frame->payload, received_picture->data_size - 4);
    received_picture->data[0] = 0;
    received_picture->data[1] = 0;
    received_picture->data[2] = 0;
    received_picture->data[3] = 1;
  }
  else
  {
    memcpy(received_picture->data.get(), frame->payload, received_picture->data_size);
  }

  (void)uvg_rtp::frame::dealloc_frame(frame);
  std::unique_ptr<Data> rp( received_picture );
  sendOutput(std::move(rp));
}
