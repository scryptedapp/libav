export enum AVPacketFlags {
  AV_PKT_FLAG_KEY = 0x0001,
  AV_PKT_FLAG_CORRUPT = 0x0002,
  AV_PKT_FLAG_DISCARD = 0x0004,
  AV_PKT_FLAG_TRUSTED = 0x0008,
  AV_PKT_FLAG_DISPOSABLE = 0x0010,
}

export enum AVFrameFlags {
  AV_FRAME_FLAG_KEY = 0x0001,
  AV_FRAME_FLAG_CORRUPT = 0x0002,
  AV_FRAME_FLAG_DISCARD = 0x0004,
  AV_FRAME_FLAG_TRUSTED = 0x0008,
  AV_FRAME_FLAG_DISPOSABLE = 0x0010,
}

export enum AVPictureType {
  AV_PICTURE_TYPE_NONE = 0, ///< Undefined
  AV_PICTURE_TYPE_I,        ///< Intra
  AV_PICTURE_TYPE_P,        ///< Predicted
  AV_PICTURE_TYPE_B,        ///< Bi-dir predicted
  AV_PICTURE_TYPE_S,        ///< S(GMC)-VOP MPEG-4
  AV_PICTURE_TYPE_SI,       ///< Switching Intra
  AV_PICTURE_TYPE_SP,       ///< Switching Predicted
  AV_PICTURE_TYPE_BI        ///< BI type
}

export enum AVCodecFlags {
  AV_CODEC_FLAG_UNALIGNED = 1 << 0,
  AV_CODEC_FLAG_QSCALE = 1 << 1,
  AV_CODEC_FLAG_4MV = 1 << 2,
  AV_CODEC_FLAG_OUTPUT_CORRUPT = 1 << 3,
  AV_CODEC_FLAG_QPEL = 1 << 4,
  AV_CODEC_FLAG_DROPCHANGED = 1 << 5,
  AV_CODEC_FLAG_RECON_FRAME = 1 << 6,
  AV_CODEC_FLAG_COPY_OPAQUE = 1 << 7,
  AV_CODEC_FLAG_FRAME_DURATION = 1 << 8,
  AV_CODEC_FLAG_PASS1 = 1 << 9,
  AV_CODEC_FLAG_PASS2 = 1 << 10,
  AV_CODEC_FLAG_LOOP_FILTER = 1 << 11,
  AV_CODEC_FLAG_GRAY = 1 << 13,
  AV_CODEC_FLAG_PSNR = 1 << 15,
  AV_CODEC_FLAG_INTERLACED_DCT = 1 << 18,
  AV_CODEC_FLAG_LOW_DELAY = 1 << 19,
  AV_CODEC_FLAG_GLOBAL_HEADER = 1 << 22,
  AV_CODEC_FLAG_BITEXACT = 1 << 23,
  AV_CODEC_FLAG_AC_PRED = 1 << 24,
  AV_CODEC_FLAG_INTERLACED_ME = 1 << 29,
  AV_CODEC_FLAG_CLOSED_GOP = 1 << 31,
}

export enum AVCodecFlags2 {
  AV_CODEC_FLAG2_FAST = 1 << 0,
  AV_CODEC_FLAG2_NO_OUTPUT = 1 << 2,
  AV_CODEC_FLAG2_LOCAL_HEADER = 1 << 3,
  AV_CODEC_FLAG2_CHUNKS = 1 << 15,
  AV_CODEC_FLAG2_IGNORE_CROP = 1 << 16,
  AV_CODEC_FLAG2_SHOW_ALL = 1 << 22,
  AV_CODEC_FLAG2_EXPORT_MVS = 1 << 28,
  AV_CODEC_FLAG2_SKIP_MANUAL = 1 << 29,
  AV_CODEC_FLAG2_RO_FLUSH_NOOP = 1 << 30,
  AV_CODEC_FLAG2_ICC_PROFILES = 1 << 31,
}

export enum AVSampleFormat {
  AV_SAMPLE_FMT_NONE = -1,
  AV_SAMPLE_FMT_U8,          ///< unsigned 8 bits
  AV_SAMPLE_FMT_S16,         ///< signed 16 bits
  AV_SAMPLE_FMT_S32,         ///< signed 32 bits
  AV_SAMPLE_FMT_FLT,         ///< float
  AV_SAMPLE_FMT_DBL,         ///< double

  AV_SAMPLE_FMT_U8P,         ///< unsigned 8 bits, planar
  AV_SAMPLE_FMT_S16P,        ///< signed 16 bits, planar
  AV_SAMPLE_FMT_S32P,        ///< signed 32 bits, planar
  AV_SAMPLE_FMT_FLTP,        ///< float, planar
  AV_SAMPLE_FMT_DBLP,        ///< double, planar
  AV_SAMPLE_FMT_S64,         ///< signed 64 bits
  AV_SAMPLE_FMT_S64P,        ///< signed 64 bits, planar

  AV_SAMPLE_FMT_NB           ///< Number of sample formats. DO NOT USE if linking dynamically
}
