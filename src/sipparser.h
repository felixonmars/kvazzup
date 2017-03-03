#pragma once


#include "common.h"

#include <QHostAddress>
#include <QString>

#include <memory>


// if strict has not been set, these can include
struct SIPMessageInfo
{
  MessageType request;
  QString version;

  QString theirName;
  QString theirUsername;
  QString theirLocation;

  QString theirTag;

  uint16_t maxForwards;

  QString ourName;
  QString ourUsername;
  QString ourLocation;
  QString ourTag;
  QList<QHostAddress> replyAddress;
  QList<QHostAddress> contactAddress;

  QString branch;

  QString callID;
  QString host;

  uint32_t cSeq;

  QString contentType;
};

  // returns a filled SIPMessageInfo if possible, otherwise
  // returns a null pointer if parsing was not successful
  std::unique_ptr<SIPMessageInfo> parseSIPMessage(QString& header);

