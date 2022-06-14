#include "videoglwidget.h"

#include "statisticsinterface.h"

#include <QPaintEvent>
#include <QCoreApplication>
#include <QEvent>
#include <QKeyEvent>
#include <QLayout>

VideoGLWidget::VideoGLWidget(QWidget* parent, uint32_t sessionID, uint32_t index, uint8_t borderSize)
  : QOpenGLWidget(parent),
  stats_(nullptr),
  sessionID_(sessionID),
  helper_(sessionID, index, borderSize)
{
  helper_.initWidget(this);

  // the new syntax does not work for some reason (unresolved overloaded function type)
  QObject::connect(this, SIGNAL(newImage()), this, SLOT(repaint()));
  QObject::connect(&helper_, &VideoDrawHelper::detach, this, &VideoGLWidget::detach);
  QObject::connect(&helper_, &VideoDrawHelper::reattach, this, &VideoGLWidget::reattach);
}


VideoGLWidget::~VideoGLWidget()
{}


void VideoGLWidget::enableOverlay()
{
  helper_.enableOverlay();
}


std::unique_ptr<int8_t[]> VideoGLWidget::getRoiMask(int& width, int& height, int qp, bool scaleToInput)
{
  return helper_.getRoiMask(width, height, qp, scaleToInput);
}


void VideoGLWidget::inputImage(std::unique_ptr<uchar[]> data, QImage &image, int64_t timestamp)
{
  drawMutex_.lock();
  // if the resolution has changed in video

  helper_.inputImage(this, std::move(data), image, timestamp);

  //update();

  emit newImage();
  drawMutex_.unlock();
}

void VideoGLWidget::paintEvent(QPaintEvent *event)
{
  Q_UNUSED(event);

  QPainter painter(this);

  if(helper_.readyToDraw())
  {
    drawMutex_.lock();

    QImage frame;
    if(helper_.getRecentImage(frame))
    {
      // sessionID 0 is the self display and we are not interested
      // update stats only for each new image.
      if(stats_ && sessionID_ != 0)
      {
        stats_->presentPackage(sessionID_, "Video");
      }
    }

    painter.drawImage(helper_.getTargetRect(), frame);
    drawMutex_.unlock();
  }
  else
  {
    painter.fillRect(event->rect(), QBrush(QColor(0,0,0)));
  }

  //QOpenGLWidget::paintEvent(event);
}

void VideoGLWidget::resizeEvent(QResizeEvent *event)
{
  QOpenGLWidget::resizeEvent(event); // its important to call this resize function, not the qwidget one.
  helper_.updateTargetRect(this);
}

void VideoGLWidget::keyPressEvent(QKeyEvent *event)
{
  helper_.keyPressEvent(this, event);
}

void VideoGLWidget::mouseDoubleClickEvent(QMouseEvent *e) {
  QWidget::mouseDoubleClickEvent(e);
  helper_.mouseDoubleClickEvent(this);
}
