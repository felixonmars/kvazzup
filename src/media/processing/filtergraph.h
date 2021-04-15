#pragma once

#include <QWidget>
#include <QAudioFormat>
#include <QObject>

#include <vector>
#include <memory>

class VideoInterface;
class StatisticsInterface;
class AudioOutputDevice;

class Filter;
class ScreenShareFilter;
class AECInputFilter;
class DisplayFilter;

typedef std::vector<std::shared_ptr<Filter>> GraphSegment;

class FilterGraph : public QObject
{
  Q_OBJECT
public:
  FilterGraph();

  void init(VideoInterface* selfView, StatisticsInterface *stats);
  void uninit();

  // These functions are used to manipulate filter graphs regarding a peer
  void sendVideoto(uint32_t sessionID, std::shared_ptr<Filter> videoFramedSource);
  void receiveVideoFrom(uint32_t sessionID, std::shared_ptr<Filter> videoSink, VideoInterface *view);
  void sendAudioTo(uint32_t sessionID, std::shared_ptr<Filter> audioFramedSource);
  void receiveAudioFrom(uint32_t sessionID, std::shared_ptr<Filter> audioSink);

  // removes participant and all its associated filter from filter graph.
  void removeParticipant(uint32_t sessionID);

  void running(bool state);

  // print the filter graph to a dot file to be drawn as a graph
  void print();

  // Refresh settings of all filters from QSettings.
  void updateSettings();

private:

  void mediaSources();

  void mic(bool state);
  void camera(bool state);
  void screenShare(bool shareState);

  // adds fitler to graph and connects it to connectIndex unless this is the first filter in graph.
  // adds format conversion if needed.
  bool addToGraph(std::shared_ptr<Filter> filter,
                  GraphSegment& graph,
                  unsigned int connectIndex = 0);

  // connects the two filters and checks for any problems
  bool connectFilters(std::shared_ptr<Filter> filter, std::shared_ptr<Filter> previous);

  // makes sure the participant exists and adds if necessary
  void checkParticipant(uint32_t sessionID);

  // iniates camera and attaches a self view to it.
  void initSelfView();

  // iniates encoder and attaches it
  void initVideoSend();

  // iniates encoder and attaches it
  void initializeAudio(bool opus);

  void removeAllParticipants();

  struct Peer
  {
    // Arrays of filters which send media, but are not connected to each other.
    std::vector<std::shared_ptr<Filter>> audioSenders; // sends audio
    std::vector<std::shared_ptr<Filter>> videoSenders; // sends video

    // Arrays of filters which receive media.
    // Each graphsegment receives one mediastream.
    std::vector<std::shared_ptr<GraphSegment>> videoReceivers;
    std::vector<std::shared_ptr<GraphSegment>> audioReceivers;
  };

  // destroy all filters associated with this peer.
  void destroyPeer(Peer* peer);

  void destroyFilters(std::vector<std::shared_ptr<Filter>>& filters);


  // key is sessionID
  std::map<uint32_t, Peer*> peers_;

 // std::shared_ptr<ScreenShareFilter> screenShare_;
  GraphSegment cameraGraph_;
  GraphSegment screenShareGraph_;
  GraphSegment audioProcessing_;

  std::shared_ptr<DisplayFilter> selfviewFilter_;

  StatisticsInterface* stats_;

  // audio configs
  QAudioFormat format_;

  QString videoFormat_;

  bool quitting_;

  std::shared_ptr<AudioOutputDevice> audioOutput_;
};
