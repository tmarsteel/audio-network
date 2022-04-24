package com.github.tmarsteel.audionetwork.transmitter

import io.kotlintest.forAll
import io.kotlintest.matchers.shouldBe
import io.kotlintest.matchers.shouldThrow
import io.kotlintest.specs.FreeSpec
import java.nio.BufferOverflowException
import kotlin.random.Random

class ByteRingBufferTest : FreeSpec({
    "new buffer" - {
        "remainingRead = 0" {
            ByteRingBuffer(100).remainingRead shouldBe 0
        }
        "remainingWrite = capacity" {
            val capacity = Random.nextInt(100, 1000)
            ByteRingBuffer(capacity).remainingWrite shouldBe capacity
        }
        "write above remaining" {
            shouldThrow<BufferOverflowException>() {
                ByteRingBuffer(100).put(ByteArray(102))
            }
        }
        "write below remaining and read back" {
            val buffer = ByteRingBuffer(100)
            buffer.put(ByteArray(40) { 2 })
            buffer.remainingRead shouldBe 40
            buffer.remainingWrite shouldBe 60

            val target = ByteArray(40)
            buffer.get(target)
            forAll(target.toList()) {
                 it shouldBe 2.toByte()
            }
        }
    }

    "write across end of buffer" {
        val buffer = ByteRingBuffer(100)
        buffer.put(ByteArray(60) { 1 })
        buffer.get(ByteArray(40))
        buffer.remainingRead shouldBe 20
        buffer.remainingWrite shouldBe 80
        buffer.put(ByteArray(70) { 2 })
        buffer.remainingRead shouldBe 90
        buffer.remainingWrite shouldBe 10

        val target1 = ByteArray(20)
        buffer.get(target1)
        forAll(target1.toList()) { it shouldBe 1.toByte() }

        buffer.remainingRead shouldBe 70
        buffer.remainingWrite shouldBe 30

        val target2 = ByteArray(70)
        buffer.get(target2)
        forAll(target2.toList()) { it shouldBe 2.toByte() }

        buffer.remainingRead shouldBe 0
        buffer.remainingWrite shouldBe 100
    }

    "write when data is across end of buffer" {
        val buffer = ByteRingBuffer(100)
        buffer.put(ByteArray(50))
        buffer.get(ByteArray(50))
        buffer.put(ByteArray(70) { 1 })
        buffer.put(ByteArray(30) { 2 })
        buffer.remainingWrite shouldBe 0
        buffer.remainingRead shouldBe 100

        val target = ByteArray(100)
        buffer.get(target)
        forAll(target.toList().subList(0, 70)) {
            it shouldBe 1.toByte()
        }
        forAll(target.toList().subList(70, 100)) {
            it shouldBe 2.toByte()
        }
    }

    "fill completely" - {
        "start to end" {
            val buffer = ByteRingBuffer(100)
            buffer.put(ByteArray(100))
            buffer.remainingRead shouldBe 100
            buffer.remainingWrite shouldBe 0
        }

        "cutover in the middle" {
            val buffer = ByteRingBuffer(100)
            buffer.put(ByteArray(50) { 1 })
            buffer.get(ByteArray(50) { 2 })
            buffer.put(ByteArray(100) { 3 })

            buffer.remainingRead shouldBe 100
            buffer.remainingWrite shouldBe 0
        }
    }
})