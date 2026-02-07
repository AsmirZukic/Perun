// Protocol Definitions

const PacketType = {
    VideoFrame: 0x01,
    AudioChunk: 0x02,
    InputEvent: 0x03,
    Config: 0x04,
    DebugInfo: 0x05
};

const PacketFlags = {
    Compressed: 0x01,
    DeltaFrame: 0x02
};

const Capabilities = {
    CAP_DELTA: 0x01,
    CAP_AUDIO: 0x02,
    CAP_DEBUG: 0x04
};

// ...
