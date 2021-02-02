#include "sipregistrations.h"


#include "initiation/transaction/sipdialogstate.h"
#include "initiation/transaction/sipclient.h"

#include "common.h"
#include "serverstatusview.h"
#include "global.h"


#include <QDebug>

const int REGISTER_SEND_PERIOD = (REGISTER_INTERVAL - 5)*1000;


SIPRegistrations::SIPRegistrations():
  retryTimer_(nullptr)
{}


SIPRegistrations::~SIPRegistrations()
{}


void SIPRegistrations::init(ServerStatusView *statusView)
{
  printNormal(this, "Initiatin Registrations");
  statusView_ = statusView;

  QObject::connect(&retryTimer_, &QTimer::timeout,
                   this, &SIPRegistrations::refreshRegistrations);

  retryTimer_.setInterval(REGISTER_SEND_PERIOD); // have 5 seconds extra to reach registrar
  retryTimer_.setSingleShot(false);
}


void SIPRegistrations::uninit()
{
  for (auto& registration : registrations_)
  {
    if (registration.second->status == REG_ACTIVE)
    {
      registration.second->status = DEREGISTERING;
      registration.second->client.transactionREGISTER(0);
    }
  }

  printNormal(this, "Finished uniniating");

  // we don't wait for the OK reply so we can quit faster.
  return;
}

void SIPRegistrations::bindToServer(QString serverAddress, QString localAddress,
                                    uint16_t port)
{
  qDebug() << "Binding to SIP server at:" << serverAddress;

  std::shared_ptr<SIPRegistrationData> data
      = std::shared_ptr<SIPRegistrationData>
      (new SIPRegistrationData{{}, {}, localAddress, port, INACTIVE});

  SIP_URI serverUri = {DEFAULT_SIP_TYPE, {"", ""}, {serverAddress, 0}, {}, {}};
  data->state.setLocalHost(localAddress);
  data->state.createServerConnection(serverUri);
  data->client.setNonDialogStuff(serverUri);
  registrations_[serverAddress] = data;

  QObject::connect(&registrations_[serverAddress]->client,
                   &SIPClient::sendNondialogRequest,
                   this, &SIPRegistrations::sendNonDialogRequest);

  statusView_->updateServerStatus("Request sent. Waiting response...");
  registrations_[serverAddress]->status = FIRST_REGISTRATION;
  registrations_[serverAddress]->client.transactionREGISTER(REGISTER_INTERVAL);
}


bool SIPRegistrations::identifyRegistration(SIPResponse& response, QString &outAddress)
{
  outAddress = "";

  // check if this is a response from the server.
  for (auto i = registrations_.begin(); i != registrations_.end(); ++i)
  {
    if(i->second->state.correctResponseDialog(response.message,
                                              response.message->cSeq.cSeq, false))
    {
      // TODO: we should check that every single detail is as specified in rfc.
      if(i->second->client.waitingResponse(response.message->cSeq.method))
      {
        printNormal(this, "Found registration matching the response");
        outAddress = i->first;
      }
      else
      {
        qDebug() << "PEER_ERROR: Found the server dialog, "
                    "but we have not sent a request to their response.";
        return false;
      }
    }
  }
  return outAddress != "";
}


void SIPRegistrations::processNonDialogResponse(SIPResponse& response)
{
  // REGISTER response must not create route. In other words ignore all record-routes

  if (response.message->cSeq.method == SIP_REGISTER)
  {
    bool foundRegistration = false;

    for (auto& i : registrations_)
    {
      if (i.first == response.message->to.address.uri.hostport.host)
      {
        QVariant content; // unused
        i.second->client.processIncomingResponse(response, content);
        if (!i.second->client.shouldBeKeptAlive())
        {
          printWarning(this, "Got a failure response to our REGISTER");
          i.second->status = INACTIVE;
          return;
        }

        if (response.type == SIP_OK)
        {
          foundRegistration = true;

          if (i.second->status != RE_REGISTRATION &&
              response.message->vias.at(0).receivedAddress != "" &&
              response.message->vias.at(0).rportValue != 0 &&
              (i.second->contactAddress != response.message->vias.at(0).receivedAddress ||
               i.second->contactPort != response.message->vias.at(0).rportValue))
          {
            printNormal(this, "Detected that we are behind NAT!");

            // we want to remove the previous registration so it doesn't cause problems
            if (i.second->status == FIRST_REGISTRATION)
            {
              printNormal(this, "Resetting previous registration");
              i.second->status = DEREGISTERING;
              i.second->client.transactionREGISTER(0);
              return;
            }
            else if (i.second->status == DEREGISTERING)// the actual NAT registration
            {
              i.second->status = RE_REGISTRATION;
              printNormal(this, "Sending the final NAT REGISTER");
              i.second->contactAddress = response.message->contact.first().address.uri.hostport.host;
              i.second->contactPort = response.message->contact.first().address.uri.hostport.port;
              // makes sure we don't end up in infinite loop if the address doesn't match

              statusView_->updateServerStatus("Behind NAT, updating address...");

              i.second->client.transactionREGISTER(REGISTER_INTERVAL); // re-REGISTER with NAT address and port
              return;
            }
            else
            {
              printError(this, "The Registration response does not match internal state");
            }
          }
          else
          {
            statusView_->updateServerStatus("Registered");
          }

          i.second->status = REG_ACTIVE;

          if (!retryTimer_.isActive())
          {
            retryTimer_.start(REGISTER_SEND_PERIOD);
          }

          printNormal(this, "Registration was successful.");
        }
        else
        {
          printDebug(DEBUG_ERROR, this, "REGISTER-request failed");
          statusView_->updateServerStatus(response.text);
        }
      }

      if (!foundRegistration)
      {
        qDebug() << "PEER ERROR: Got a resonse to REGISTRATION we didn't send";
      }
    }
  }
  else
  {
    printUnimplemented(this, "Processing of Non-REGISTER requests");
  }
}


void SIPRegistrations::refreshRegistrations()
{
  // no need to continue refreshing if we have no active registrations
  if (!haveWeRegistered())
  {
    printWarning(this, "Not refreshing our registrations, because we have none!");
    retryTimer_.stop();
    return;
  }

  for (auto& i : registrations_)
  {
    if (i.second->status == REG_ACTIVE)
    {
      statusView_->updateServerStatus("Second request sent. Waiting response...");
      i.second->client.transactionREGISTER(REGISTER_INTERVAL);
    }
  }
}


bool SIPRegistrations::haveWeRegistered()
{
  bool registered = false;

  for (auto& i : registrations_)
  {
    if (i.second->status == REG_ACTIVE)
    {
      registered = true;
    }
  }

  return registered;
}


void SIPRegistrations::sendNonDialogRequest(SIP_URI& uri, SIPRequest& request)
{
  printNormal(this, "Send non-dialog request");

  if (request.method == SIP_REGISTER)
  {
    if (registrations_.find(uri.hostport.host) == registrations_.end())
    {
      printDebug(DEBUG_PROGRAM_ERROR, this,
                 "Registration information should have been created "
                 "already before sending REGISTER message!");

      return;
    }
    QVariant content; // unused

    registrations_[uri.hostport.host]->state.processOutgoingRequest(request, content);
  }

  emit transportProxyRequest(uri.hostport.host, request);
}
