package com.github.tmarsteel.audionetwork.transmitter

import tomp2p.opuswrapper.Opus
import java.nio.BufferUnderflowException

internal fun throwOnOpusError(errorCode: Int) {
    when (errorCode) {
        Opus.OPUS_OK               -> {}
        Opus.OPUS_BAD_ARG          -> throw IllegalArgumentException("Opus BAD_ARG")
        Opus.OPUS_BUFFER_TOO_SMALL -> throw BufferUnderflowException()
        Opus.OPUS_INTERNAL_ERROR   -> throw RuntimeException("Opus INTERNAL_ERROR")
        Opus.OPUS_INVALID_PACKET   -> throw IllegalArgumentException("Opus INVALID_PACKET")
        Opus.OPUS_UNIMPLEMENTED    -> throw UnsupportedOperationException("Opus UNIMPLEMENTED")
        Opus.OPUS_INVALID_STATE    -> throw IllegalStateException("Opus INVALID_STATE")
        Opus.OPUS_ALLOC_FAIL       -> throw RuntimeException("Opus ALLOC_FAIL")
    }
}

