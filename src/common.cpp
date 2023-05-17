
#include "common.h"
#include "settingskeys.h"

#include "initiation/negotiation/sdptypes.h"

#include "logger.h"

// Didn't find sleep in QCore
#ifdef Q_OS_WIN
#include <winsock2.h> // for windows.h
#include <windows.h> // for Sleep
#endif

#include <QSettings>
#include <QMutex>
#include <QNetworkInterface>

#include <cstdlib>


const QString alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
                         "abcdefghijklmnopqrstuvwxyz"
                         "0123456789";


QString generateRandomString(uint32_t length)
{
  // TODO make this cryptographically secure to avoid collisions
  QString string;
  for( unsigned int i = 0; i < length; ++i )
  {
    string.append(alphabet.at(rand()%alphabet.size()));
  }
  return string;
}


bool settingEnabled(QString key)
{
  return settingValue(key) == 1;
}


int settingValue(QString key)
{
  QSettings settings(settingsFile, settingsFileFormat);

  if (!settings.value(key).isValid())
  {
    Logger::getLogger()->printDebug(DEBUG_WARNING, "Common", "Found faulty setting", 
                                    {"Key"}, {key});
    return 0;
  }

  return settings.value(key).toInt();
}


QString settingString(QString key)
{
  QSettings settings(settingsFile, settingsFileFormat);

  if (!settings.value(key).isValid())
  {
    Logger::getLogger()->printDebug(DEBUG_WARNING, "Common", "Found faulty setting", 
                                   {"Key"}, {key});
    return "";
  }

  return settings.value(key).toString();
}


QString getLocalUsername()
{
  QSettings settings(settingsFile, settingsFileFormat);

  return !settings.value(SettingsKey::localUsername).isNull()
      ? settings.value(SettingsKey::localUsername).toString() : "anonymous";
}


bool isLocalCandidate(std::shared_ptr<ICEInfo> info)
{
  QString candidateAddress = info->address;

  if (info->rel_address != "")
  {
    candidateAddress = info->rel_address;
  }

  for (const QHostAddress& localInterface : QNetworkInterface::allAddresses())
  {
    if (localInterface.toString() == candidateAddress)
    {
      return true;
    }
  }

  return false;
}


bool getSendAttribute(const MediaInfo &media, bool local)
{
  bool send = false;
  if (local)
  {
    send = (media.flagAttributes.empty()
                 || media.flagAttributes.at(0) == A_NO_ATTRIBUTE
                 || media.flagAttributes.at(0) == A_SENDRECV
                 || media.flagAttributes.at(0) == A_SENDONLY);
  }
  else
  {
    send = (media.flagAttributes.empty()
                 || media.flagAttributes.at(0) == A_NO_ATTRIBUTE
                 || media.flagAttributes.at(0) == A_SENDRECV
                 || media.flagAttributes.at(0) == A_RECVONLY);
  }

  return send;
}


bool getReceiveAttribute(const MediaInfo &media, bool local)
{
  bool receive = false;
  if (local)
  {
    receive = (media.flagAttributes.empty()
                 || media.flagAttributes.at(0) == A_NO_ATTRIBUTE
                 || media.flagAttributes.at(0) == A_SENDRECV
                 || media.flagAttributes.at(0) == A_RECVONLY);
  }
  else
  {
    receive = (media.flagAttributes.empty()
                 || media.flagAttributes.at(0) == A_NO_ATTRIBUTE
                 || media.flagAttributes.at(0) == A_SENDRECV
                 || media.flagAttributes.at(0) == A_SENDONLY);
  }

  return receive;
}


void setSDPAddress(QString inAddress, QString& address, QString& nettype, QString& addressType)
{
  address = inAddress;
  nettype = "IN";

  // TODO: Improve the address detection
  if (inAddress.front() == '[')
  {
    address = inAddress.mid(1, inAddress.size() - 2);
    addressType = "IP6";
  }
  else
  {
    addressType = "IP4";
  }
}


bool containCandidates(std::vector<std::shared_ptr<ICEPair>> &streams,
                            std::vector<std::shared_ptr<ICEPair>> allCandidates)
{
  int matchingStreams = 0;
  for (auto& candidatePair : allCandidates)
  {
    for (auto& readyConnection : streams)
    {
      if (sameCandidate(readyConnection->local, candidatePair->local) &&
          sameCandidate(readyConnection->remote, candidatePair->remote))
      {
        ++matchingStreams;
      }
    }
  }

  return streams.size() == matchingStreams;
}


bool sameCandidates(std::vector<std::shared_ptr<ICEPair> > newCandidates,
                         std::vector<std::shared_ptr<ICEPair> > oldCandidates)
{
  if (newCandidates.empty() || oldCandidates.empty())
  {
    return false;
  }

  for (auto& newCandidate: newCandidates)
  {
    bool candidatesFound = false;

    for (auto& oldCandidate: oldCandidates)
    {
      if (sameCandidate(newCandidate->local, oldCandidate->local) &&
          sameCandidate(newCandidate->remote, oldCandidate->remote))
      {
        // we have a match!
        candidatesFound = true;
      }
    }

    if(!candidatesFound)
    {
      // could not find a match for this candidate, which means we have to perform ICE
      return false;
    }
  }

  return true;
}


bool sameCandidate(std::shared_ptr<ICEInfo> firstCandidate,
                        std::shared_ptr<ICEInfo> secondCandidate)
{
  return firstCandidate->foundation == secondCandidate->foundation &&
      firstCandidate->component == secondCandidate->component &&
      firstCandidate->transport == secondCandidate->transport &&
      firstCandidate->address == secondCandidate->address &&
      firstCandidate->port == secondCandidate->port &&
      firstCandidate->type == secondCandidate->type &&
      firstCandidate->rel_address == secondCandidate->rel_address &&
      firstCandidate->rel_port == secondCandidate->rel_port;
}


void printIceCandidates(QString text, QList<std::shared_ptr<ICEInfo>> candidates)
{
  QStringList names;
  QStringList values;
  for (auto& candidate : candidates)
  {
    names.append("Candidate");
    if (candidate->type == "srflx" || candidate->type == "prflx")
    {
      values.append(candidate->rel_address + ":" + QString::number(candidate->rel_port));
    }
    else
    {
      values.append(candidate->address + ":" + QString::number(candidate->port));
    }
  }

  Logger::getLogger()->printDebug(DEBUG_NORMAL, "Common", text, names, values);
}
