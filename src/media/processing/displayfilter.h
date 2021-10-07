#pragma once
#include "filter.h"

class VideoInterface;

class DisplayFilter : public Filter
{
public:
  DisplayFilter(QString id, StatisticsInterface* stats,
                std::shared_ptr<HWResourceManager> hwResources,
                VideoInterface *widget, uint32_t peer);
  ~DisplayFilter();

  void updateSettings();

  // Set whether we should mirror the incoming video depending upon what optimizations do.
  // Settings can also affect this.
  void setProperties(bool mirrorHorizontal, bool mirrorVertical)
  {
    horizontalMirroring_ = mirrorHorizontal;
    verticalMirroring_ = mirrorVertical;
  }

protected:
  void process();

private:

  bool horizontalMirroring_;
  bool verticalMirroring_;
  bool flipEnabled_;

  // Owned by Conference view
  VideoInterface* widget_;

  uint32_t sessionID_;
};
