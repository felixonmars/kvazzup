#include "sipregistrations.h"


#include "initiation/transaction/sipdialogstate.h"
#include "initiation/transaction/sipnondialogclient.h"

#include "common.h"
#include "serverstatusview.h"

#include <QDebug>


SIPRegistrations::SIPRegistrations()
{

}


void SIPRegistrations::init(SIPTransactionUser *callControl, ServerStatusView *statusView)
{
  printNormal(this, "Initiatin Registrations");
  transactionUser_ = callControl;
  statusView_ = statusView;
}


bool SIPRegistrations::uninit()
{
  for (auto& registration : registrations_)
  {
    if (registration.second.client->waitingResponse(SIP_REGISTER))
    {
      printWarning(this, "Cannot delete because of an ongoing Registration.");
      return false;
    }
  }

  printNormal(this, "Successfully uninited registrations");
  return true;
}

void SIPRegistrations::bindToServer(QString serverAddress, QString localAddress,
                                    uint16_t port)
{
  qDebug() << "Binding to SIP server at:" << serverAddress;

  Q_ASSERT(transactionUser_);
  if(transactionUser_)
  {
    SIPRegistrationData data = {std::shared_ptr<SIPNonDialogClient> (new SIPNonDialogClient(transactionUser_)),
                                std::shared_ptr<SIPDialogState> (new SIPDialogState()),
                                localAddress, port, false, false};

    SIP_URI serverUri = {TRANSPORTTYPE, "", "", serverAddress, 0, {}};
    data.state->createServerConnection(serverUri);
    data.client->init();
    data.client->set_remoteURI(serverUri);
    registrations_[serverAddress] = data;

    QObject::connect(registrations_[serverAddress].client.get(),
                     &SIPNonDialogClient::sendNondialogRequest,
                     this, &SIPRegistrations::sendNonDialogRequest);
    statusView_->updateServerStatus(ServerStatus::IN_PROCESS);

    registrations_[serverAddress].client->registerToServer();
  }
  else
  {
    printProgramError(this, "Not initialized with transaction user");
  }
}


bool SIPRegistrations::identifyRegistration(SIPResponse& response, QString &outAddress)
{
  outAddress = "";

  // check if this is a response from the server.
  for (auto i = registrations_.begin(); i != registrations_.end(); ++i)
  {
    if(i->second.state->correctResponseDialog(response.message->dialog,
                                              response.message->cSeq, false))
    {
      // TODO: we should check that every single detail is as specified in rfc.
      if(i->second.client->waitingResponse(response.message->transactionRequest))
      {
        qDebug() << "Found dialog matching the response";
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

  if (response.message->transactionRequest == SIP_REGISTER)
  {

    bool foundRegistration = false;

    for (auto& i : registrations_)
    {
      if (i.first == response.message->to.host)
      {
        if (!i.second.client->processResponse(response, i.second.state))
        {
          printWarning(this, "Got a failure response to our REGISTER");
          return;
        }

        if (response.type == SIP_OK)
        {
          i.second.active = true;

          foundRegistration = true;

          if (!i.second.updatedContact &&
              response.message->vias.at(0).receivedAddress != "" &&
              response.message->vias.at(0).rportValue != 0 &&
              (i.second.contactAddress != response.message->vias.at(0).receivedAddress ||
               i.second.contactPort != response.message->vias.at(0).rportValue))
          {

            qDebug() << "Detected that we are behind NAT! Sending a second REGISTER";

            i.second.contactAddress = response.message->contact.host;
            i.second.contactPort = response.message->contact.port;

            // makes sure we don't end up in infinite loop if the address doesn't match
            i.second.updatedContact = true;

            statusView_->updateServerStatus(ServerStatus::BEHIND_NAT);

            i.second.client->registerToServer(); // re-REGISTER with NAT address and port
          }
          else
          {
            statusView_->updateServerStatus(ServerStatus::REGISTERED);
          }

          printNormal(this, "Registration was succesful.");
        }
        else
        {
          printDebug(DEBUG_ERROR, this, "REGISTER-request failed");
          statusView_->updateServerStatus(ServerStatus::SERVER_FAILED);
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


bool SIPRegistrations::haveWeRegistered()
{
  bool registered = false;

  for (auto& i : registrations_)
  {
    if (i.second.active)
    {
      registered = true;
    }
  }

  return registered;
}


void SIPRegistrations::sendNonDialogRequest(SIP_URI& uri, RequestType type)
{
  qDebug() << "Start sending of a non-dialog request. Type:" << type;
  SIPRequest request;
  request.type = type;

  if (type == SIP_REGISTER)
  {
    if (registrations_.find(uri.host) == registrations_.end())
    {
      printDebug(DEBUG_PROGRAM_ERROR, this,
                 "Registration information should have been created "
                 "already before sending REGISTER message!");

      return;
    }

    registrations_[uri.host].client->getRequestMessageInfo(request.type, request.message);
    registrations_[uri.host].state->getRequestDialogInfo(request);

    QVariant content; // we dont have content in REGISTER
    emit transportProxyRequest(uri.host, request);
  }
  else if (type == SIP_OPTIONS) {
    printDebug(DEBUG_PROGRAM_ERROR, this,
                     "Trying to send unimplemented non-dialog request OPTIONS!");
  }
  else {
    printDebug(DEBUG_PROGRAM_ERROR, this,
                     "Trying to send a non-dialog request of type which is a dialog request!");
  }
}
