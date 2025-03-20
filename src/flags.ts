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
