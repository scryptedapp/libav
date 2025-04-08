#include <napi.h>
extern "C"
{
#include <libavcodec/avcodec.h>
#include <libavformat/avformat.h>
#include <libavutil/hwcontext.h>
#include <libavutil/frame.h>
#include <libavfilter/buffersink.h>
#include <libavfilter/buffersrc.h>
#include <libavfilter/avfilter.h>
#include <libavutil/opt.h>
}

#include <thread>


#include "error.h"

std::string AVErrorString(int errnum)
{
    char errbuf[256];
    std::string error;

    if (av_strerror(errnum, errbuf, sizeof(errbuf)) < 0)
    {
        error = "Unknown error code: " + std::to_string(errnum);
    }
    else
    {
        error = errbuf;
    }
    return error;
}
