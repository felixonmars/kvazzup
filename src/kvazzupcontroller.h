#pragma once

#include "media/mediamanager.h"
#include "initiation/sipmanager.h"
#include "initiation/siptransactionuser.h"
#include "ui/gui/callwindow.h"
#include "participantinterface.h"

#include <QObject>

#include <map>


/* This is the main class of the program and thus should control what, in which order
 * and when the other modules do tasks.
 */


struct SDPMessageInfo;
class StatisticsInterface;

class KvazzupController :
    public QObject,
    public ParticipantInterface,
    public SIPTransactionUser
{
  Q_OBJECT
public:
  KvazzupController();

  void init();
  void uninit();

  // participant interface funtions used to start a call or a chat.
  virtual uint32_t callToParticipant(QString name, QString username, QString ip);
  virtual uint32_t chatWithParticipant(QString name, QString username, QString ip);

  // Call Control Interface used by SIP transaction
  virtual void outgoingCall(uint32_t sessionID, QString callee);
  virtual bool incomingCall(uint32_t sessionID, QString caller);
  virtual void callRinging(uint32_t sessionID);
  virtual void peerAccepted(uint32_t sessionID);
  virtual void callNegotiated(uint32_t sessionID);
  virtual void cancelIncomingCall(uint32_t sessionID);
  virtual void endCall(uint32_t sessionID);
  virtual void failure(uint32_t sessionID, QString error);
  virtual void registeredToServer();
  virtual void registeringFailed();

public slots:  

  // reaction to user GUI interactions
  void updateSettings(); // update all the components that use settings
  void micState();       // change mic state
  void cameraState();    // change camera state
  void shareState();     // update screen share
  void endTheCall();     // user wants to end the call
  void windowClosed();   // user has closed the window
  void userAcceptsCall(uint32_t sessionID); // user has accepted the incoming call
  void userRejectsCall(uint32_t sessionID); // user has rejected the incoming call
  void userCancelsCall(uint32_t sessionID); // user has rejected the incoming call

  void iceCompleted(quint32 sessionID,
                    const std::shared_ptr<SDPMessageInfo> local,
                    const std::shared_ptr<SDPMessageInfo> remote);
  void iceFailed(quint32 sessionID);

  void delayedAutoAccept();
private:
  void removeSession(uint32_t sessionID, QString message, bool temporaryMessage);

  void createSingleCall(uint32_t sessionID);
  void setupConference();

  // Call state is a non-dependant way
  enum CallState {
    CALLRINGINGWITHUS,
    CALLINGTHEM,
    CALLRINGINWITHTHEM,
    CALLNEGOTIATING,
    CALLWAITINGICE,
    CALLONGOING
  };

  struct SessionState {
    CallState state;
    std::shared_ptr<SDPMessageInfo> localSDP;
    std::shared_ptr<SDPMessageInfo> remoteSDP;
  };

  std::map<uint32_t, SessionState> states_;

  MediaManager media_; // Media processing and delivery
  SIPManager sip_; // SIP
  CallWindow window_; // GUI

  StatisticsInterface* stats_;

  QTimer delayAutoAccept_;
  uint32_t delayedAutoAccept_;
};
