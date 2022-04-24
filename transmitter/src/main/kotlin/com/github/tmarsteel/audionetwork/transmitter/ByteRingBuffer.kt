package com.github.tmarsteel.audionetwork.transmitter

import java.nio.BufferOverflowException
import java.nio.BufferUnderflowException

class ByteRingBuffer(val capacity: Int) {
    private val buffer = ByteArray(capacity)
    private var dataStart = 0
    var remainingRead = 0
        private set

    val remainingWrite: Int
        get() = buffer.size - remainingRead

    fun put(source: ByteArray, offset: Int, length: Int) {
        require(source.size >= offset + length)
        if (remainingWrite < length) {
            throw BufferOverflowException()
        }

        val nBytesUntilEnd = buffer.size - (dataStart + remainingRead)
        if (nBytesUntilEnd >= length) {
            System.arraycopy(source, offset, this.buffer, dataStart + remainingRead, length)
            remainingRead += length
            return
        }

        if (nBytesUntilEnd <= 0) {
            System.arraycopy(source, offset, this.buffer, (dataStart + remainingRead) % buffer.size, length)
            remainingRead += length
            return
        }

        put(source, offset, nBytesUntilEnd)
        put(source, offset + nBytesUntilEnd, length - nBytesUntilEnd)
    }

    fun put(source: ByteArray) = put(source, 0, source.size)
    fun put(byte: Byte) = put(ByteArray(1) { byte }, 0, 1)

    fun get(target: ByteArray, offset: Int, length: Int) {
        require(target.size >= offset + length)
        if (remainingRead < length) {
            throw BufferUnderflowException()
        }

        val nBytesUntilEnd = buffer.size - dataStart
        if (nBytesUntilEnd >= length) {
            System.arraycopy(buffer, dataStart, target, offset, length)
            dataStart = (dataStart + length) % buffer.size
            remainingRead -= length
            return
        }

        get(target, offset, nBytesUntilEnd)
        get(target, offset + nBytesUntilEnd, length - nBytesUntilEnd)
    }

    fun get(target: ByteArray) = get(target, 0, target.size)
    fun get(): Byte {
        val target = ByteArray(1)
        get(target)
        return target[0]
    }
}