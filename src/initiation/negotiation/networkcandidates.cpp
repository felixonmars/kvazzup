#include "networkcandidates.h"

#include "common.h"

#include <QNetworkInterface>
#include <QUdpSocket>
#include <QDebug>

const QString STUN_SERVER = "stun.l.google.com";
const uint16_t GOOGLE_STUN_PORT = 19302;
const uint16_t STUNADDRESSPOOL = 5;


NetworkCandidates::NetworkCandidates():
  requests_(),
  stunAddresses_(),
  stunBindings_(),
  portLock_(),
  availablePorts_(),
  reservedPorts_()
{}


NetworkCandidates::~NetworkCandidates()
{
  requests_.clear();
}


void NetworkCandidates::setPortRange(uint16_t minport,
                                     uint16_t maxport)
{
  if(minport >= maxport)
  {
    printProgramError(this, "Min port is smaller or equal to max port");
  }

  foreach (const QHostAddress& address, QNetworkInterface::allAddresses())
  {
    if (address.protocol() == QAbstractSocket::IPv4Protocol &&
        !address.isLoopback())
    {
      if (sanityCheck(address, minport))
      {
        availablePorts_.insert(std::pair<QString, std::deque<uint16_t>>(address.toString(),{}));

        for(uint16_t i = minport; i < maxport; i += 2)
        {
          makePortPairAvailable(address.toString(), i);
        }
      }
    }
  }

  // get ip address of stun server
  wantAddress(STUN_SERVER);

  QObject::connect(&refreshSTUNTimer_,  &QTimer::timeout,
                   this,                &NetworkCandidates::refreshSTUN);

  // TODO: Probably best way to do this is periodically every 10 minutes or so.
  // That way we get our current STUN address
  refreshSTUNTimer_.setInterval(100);
  refreshSTUNTimer_.setSingleShot(false);
  refreshSTUNTimer_.start();
}


void NetworkCandidates::refreshSTUN()
{
  if (!stunServerAddress_.isNull() &&
      stunAddresses_.size() + requests_.size() < STUNADDRESSPOOL)
  {
    // Send STUN-Request through all the interfaces, since we don't know which is
    // connected to the internet. Most of them will fail.
    moreSTUNCandidates();
  }

  // The socket unbinding cannot happen in processreply, because that would destroy the socket
  // leading to heap corruption. Instead we do the unbinding here.

  QStringList removed;
  for (auto& request : requests_)
  {
    if (request.second->finished)
    {
      request.second->udp.unbind();
      removed.push_back(request.first);
    }
  }

  // remove requests so we know how many reaquests we have going on.
  for (auto& removal : removed)
  {
    requests_.erase(removal);
    //printNormal(this, "Removed", {"Left"}, {QString::number(requests_.size())});
  }
}


void NetworkCandidates::createSTUNCandidate(QHostAddress local, quint16 localPort,
                                            QHostAddress stun, quint16 stunPort)
{
  if (stun == QHostAddress(""))
  {
    printDebug(DEBUG_WARNING, "ICE",
       "Failed to resolve public IP! Server-reflexive candidates won't be created!");
    return;
  }

  printNormal(this, "Created ICE STUN candidate", {"STUN Translation"},
            {local.toString() + ":" + QString::number(localPort) + " << " +
             stun.toString() + ":" + QString::number(stunPort)});

  stunAddresses_.push_back({stun, stunPort});
  stunBindings_.push_back({local, localPort});
}


std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> NetworkCandidates::localCandidates(
    uint8_t streams, uint32_t sessionID)
{
  std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> addresses
      =   std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> (new QList<std::pair<QHostAddress, uint16_t>>());

  for (auto& interface : availablePorts_)
  {
    if (isPrivateNetwork(interface.first))
    {
      for (unsigned int i = 0; i < streams; ++i)
      {
        addresses->push_back({QHostAddress(interface.first),
                              nextAvailablePortPair(interface.first, sessionID)});
      }
    }
  }

  return addresses;
}


std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> NetworkCandidates::globalCandidates(
    uint8_t streams, uint32_t sessionID)
{
  std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> addresses
      =   std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> (new QList<std::pair<QHostAddress, uint16_t>>());

  for (auto& interface : availablePorts_)
  {
    if (!isPrivateNetwork(interface.first))
    {
      for (unsigned int i = 0; i < streams; ++i)
      {
        addresses->push_back({QHostAddress(interface.first),
                              nextAvailablePortPair(interface.first, sessionID)});
      }
    }
  }
  return addresses;
}


std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> NetworkCandidates::stunCandidates(
    uint8_t streams)
{ 
  std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> addresses
      =   std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> (new QList<std::pair<QHostAddress, uint16_t>>());
  if (!stunAddresses_.empty())
  {
    for (unsigned int i = 0; i < streams; ++i)
    {
      std::pair<QHostAddress, uint16_t> address = stunAddresses_.front();
      stunAddresses_.pop_front();
      addresses->push_back(address);
    }

  }
  else
  {
    printWarning(this, "No STUN candidates found!");
  }

  return addresses;
}


std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> NetworkCandidates::stunBindings(
    uint8_t streams, uint32_t sessionID)
{
  std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> addresses
      =   std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> (new QList<std::pair<QHostAddress, uint16_t>>());

  if (!stunBindings_.empty())
  {
    for (unsigned int i = 0; i < streams; ++i)
    {
      std::pair<QHostAddress, uint16_t> address = stunBindings_.front();
      stunBindings_.pop_front();

      addresses->push_back(address);

      reservedPorts_[sessionID].push_back(
            std::pair<QString, uint16_t>({address.first.toString(), address.second}));
    }
  }
  else
  {
    printWarning(this, "No STUN bindings added!");
  }

  return addresses;
}


std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> NetworkCandidates::turnCandidates(
    uint8_t streams, uint32_t sessionID)
{
  Q_UNUSED(streams);
  Q_UNUSED(sessionID);

  // We are probably never going to support TURN addresses.
  // If we are, add candidates to the list.
  std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> addresses
      =   std::shared_ptr<QList<std::pair<QHostAddress, uint16_t>>> (new QList<std::pair<QHostAddress, uint16_t>>());
  return addresses;
}


uint16_t NetworkCandidates::nextAvailablePortPair(QString interface, uint32_t sessionID)
{


  uint16_t newLowerPort = 0;

  portLock_.lock();

  if (availablePorts_.find(interface) == availablePorts_.end() ||
      availablePorts_[interface].size() == 0)
  {
    portLock_.unlock();
    printWarning(this, "Either couldn't find interface or "
                       "this interface has run out of ports");
    return 0;
  }

  newLowerPort = availablePorts_[interface].at(0);
  availablePorts_[interface].pop_front();
  reservedPorts_[sessionID].push_back(std::pair<QString, uint16_t>(interface, newLowerPort));

  // This is because of a hack in ICE which uses upper ports for opus instead of allocating new ones
  // TODO: Remove once ice supports any amount of media streams.
  availablePorts_[interface].pop_front();
  reservedPorts_[sessionID].push_back(std::pair<QString, uint16_t>(interface, newLowerPort + 2));

  /*
  // TODO: I'm suspecting this may sometimes hang Kvazzup at the start
  if(availablePorts_.size() >= 1)
  {
    QUdpSocket test_port1;
    QUdpSocket test_port2;
    do
    {
      newLowerPort = availablePorts_.at(0);
      availablePorts_.pop_front();
      qDebug() << "Trying to bind ports:" << newLowerPort << "and" << newLowerPort + 1;

    } while(!test_port1.bind(newLowerPort) && !test_port2.bind(newLowerPort + 1));
    test_port1.abort();
    test_port2.abort();
  }
  else
  {
    qDebug() << "Could not reserve ports. Ports available:" << availablePorts_.size();
  }
  */
  portLock_.unlock();

  //printDebug(DEBUG_NORMAL, "SDP Parameter Manager",
  //           "Binding finished", {"Bound lower port"}, {QString::number(newLowerPort)});

  return newLowerPort;
}

void NetworkCandidates::makePortPairAvailable(QString interface, uint16_t lowerPort)
{


  if(lowerPort != 0)
  {
    portLock_.lock();
    if (availablePorts_.find(interface) == availablePorts_.end())
    {
      portLock_.unlock();
      printWarning(this, "Couldn't find interface when making ports available.");
      return;
    }
    //qDebug() << "Freed ports:" << lowerPort << "/" << lowerPort + 1;

    availablePorts_[interface].push_back(lowerPort);
    portLock_.unlock();
  }
}


/* https://en.wikipedia.org/wiki/Private_network#Private_IPv4_addresses */
bool NetworkCandidates::isPrivateNetwork(const QString& address)
{
  if (address.startsWith("10.") || address.startsWith("192.168"))
  {
    return true;
  }

  if (address.startsWith("172."))
  {
    int octet = address.split(".")[1].toInt();

    if (octet >= 16 && octet <= 31)
    {
      return true;
    }
  }

  return false;
}


void NetworkCandidates::cleanupSession(uint32_t sessionID)
{
  if (reservedPorts_.find(sessionID) == reservedPorts_.end())
  {
    printWarning(this, "Tried to cleanup session with no reserved ports");
    return;
  }

  for(auto& session : reservedPorts_[sessionID])
  {
    makePortPairAvailable(session.first, session.second);
  }
  reservedPorts_.erase(sessionID);
}


void NetworkCandidates::wantAddress(QString stunServer)
{
  // To find the IP address of qt-project.org
  QHostInfo::lookupHost(stunServer, this, SLOT(handleStunHostLookup(QHostInfo)));
}


void NetworkCandidates::handleStunHostLookup(QHostInfo info)
{
  if (availablePorts_.empty())
  {
    printProgramError(this, "Interfaces have not been set before sending STUN requests");
    return;
  }

  const auto addresses = info.addresses();
  if(addresses.size() != 0)
  {
    stunServerAddress_ = addresses.at(0);
  }
  else
  {
    return;
  }
}

void NetworkCandidates::moreSTUNCandidates()
{
  if (!stunServerAddress_.isNull())
  {
    if(stunAddresses_.size() < STUNADDRESSPOOL)
    {
      for (auto& interface : availablePorts_)
      {
        // use 0 as STUN sessionID
        sendSTUNserverRequest(QHostAddress(interface.first), nextAvailablePortPair(interface.first, 0),
                              stunServerAddress_,                 GOOGLE_STUN_PORT);
      }
    }
  }
  else
  {
    printProgramError(this, "STUN server address not set!");
  }
}


void NetworkCandidates::sendSTUNserverRequest(QHostAddress localAddress,
                                              uint16_t localPort,
                                              QHostAddress serverAddress,
                                              uint16_t serverPort)
{
  if (localPort == 0 || serverPort == 0)
  {
    printProgramError(this, "Not port set. Can't get STUN address.");
    return;
  }

  //printNormal(this, "Sending STUN server request", {"Path"},
  //        {localAddress.toString() + ":" + QString::number(localPort) + " -> " +
  //         serverAddress.toString() + ":" + QString::number(serverPort)});

  QString key = localAddress.toString() + ":" + QString::number(localPort);

  if (requests_.find(key) == requests_.end())
  {
    requests_[key] = std::shared_ptr<STUNRequest>(new STUNRequest);
    requests_[key]->finished = false;
  }

  QObject::connect(&requests_[key]->udp, &UDPServer::datagramAvailable,
                   this,              &NetworkCandidates::processSTUNReply);

  if (!requests_[key]->udp.bindSocket(localAddress, localPort))
  {
    requests_.erase(key);
    return;
  }


  STUNMessage request = requests_[key]->message.createRequest();
  QByteArray message  = requests_[key]->message.hostToNetwork(request);

  requests_[key]->message.cacheRequest(request);

  // udp_ records localport when binding so we don't hate specify it here
  if(!requests_[key]->udp.sendData(message, localAddress,
                                   serverAddress, serverPort))
  {
    requests_.erase(key);
  }
}


void NetworkCandidates::processSTUNReply(const QNetworkDatagram& packet)
{
  if(packet.data().size() < 20)
  {
    printDebug(DEBUG_WARNING, "STUN",
        "Received too small response to STUN query!");
    return;
  }

  QString key = packet.destinationAddress().toString() + ":"
      + QString::number(packet.destinationPort());

  if (requests_.find(key) == requests_.end())
  {
    printWarning(this, "Got stun request to an interface which has not sent one!",
      {"Interface"}, {packet.destinationAddress().toString() + ":" +
                      QString::number(packet.destinationPort())});
    return;
  }

  QByteArray data = packet.data();

  QString message = QString::fromStdString(data.toHex().toStdString());
  STUNMessage response = requests_[key]->message.networkToHost(data);
  // free socket for further use


  if (!requests_[key]->message.validateStunResponse(response))
  {
    printWarning(this, "Invalid STUN response from server!", {"Message"}, {message});
    return;
  }

  std::pair<QHostAddress, uint16_t> addressInfo;

  if (response.getXorMappedAddress(addressInfo))
  {
    createSTUNCandidate(packet.destinationAddress(), packet.destinationPort(),
                        addressInfo.first, addressInfo.second);
  }
  else
  {
    printError(this, "STUN response sent by server was not Xor-mapped! Discarding...");
  }

  requests_[key]->finished = true;

  //printNormal(this, "STUN reply processed");
}


// Tries to bind to port and send a UDP packet just to check
// if it is worth including in candidates
bool NetworkCandidates::sanityCheck(QHostAddress interface, uint16_t port)
{
  QUdpSocket testSocket;

  if (!testSocket.bind(interface, port))
  {
    printNormal(this, "Could not bind to socket. Not adding to candidates", {"Port"}, {
                  interface.toString() + ":" + QString::number(port)});
    return false;
  }
  QByteArray data = QString("TestString").toLatin1();
  QNetworkDatagram datagram = QNetworkDatagram(data, interface, port + 1);
  datagram.setSender(interface, port);

  if (testSocket.writeDatagram(datagram) < 0)
  {
    testSocket.abort();
    printNormal(this, "Could not send data with socket. Not adding to candidates", {"Port"}, {
                  interface.toString() + ":" + QString::number(port)});
    return false;
  }

  testSocket.abort();

  return true;
}
