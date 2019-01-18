#pragma once

#include "gui/videointerface.h"
#include "videodrawhelper.h"

#include <QPainter>
#include <QRect>
#include <QSize>
#include <QImage>
#include <QMutex>

#include <QtOpenGL>

#include <deque>
#include <memory>


// TODO: duplicate code in this and videowidget.

class StatisticsInterface;

class VideoGLWidget : public QOpenGLWidget, public VideoInterface
{
  Q_OBJECT
  Q_INTERFACES(VideoInterface)
public:
  VideoGLWidget(QWidget* parent = nullptr, uint32_t sessionID = 0, uint8_t borderSize = 1);
  ~VideoGLWidget();

  void setStats(StatisticsInterface* stats)
  {
    stats_ = stats;
  }

  // Takes ownership of the image data
  void inputImage(std::unique_ptr<uchar[]> data, QImage &image);

  virtual VideoFormat supportedFormat()
  {
    return VIDEO_RGB32;
  }

  static unsigned int number_;

signals:

  // for reattaching after fullscreenmode
  void reattach(uint32_t sessionID_, QWidget* view);
  void deattach(uint32_t sessionID_, QWidget* view);

  void newImage();
protected:

  // QOpenGLwidget events
  void paintEvent(QPaintEvent *event);
  void resizeEvent(QResizeEvent *event);
  void keyPressEvent(QKeyEvent *event);

  void mouseDoubleClickEvent(QMouseEvent *e);

private:

  QMutex drawMutex_;

  StatisticsInterface* stats_;
  uint32_t sessionID_;

  VideoDrawHelper helper_;
};
