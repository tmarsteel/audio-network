import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.Inet4Address
import java.net.InetSocketAddress
import java.net.NetworkInterface
import kotlin.concurrent.thread

fun main(args: Array<String>) {
    BroadcastReceiver(InetSocketAddress(58765))
    println("Broadcast receiver started")
}

fun installBroadcastReceiver(iface: NetworkInterface) {
    val broadcastAddress = iface.interfaceAddresses
        .map { it.broadcast }
        .filterIsInstance<Inet4Address>()
        .firstOrNull()
        ?: run {
            println("Ignoring interface ${iface.displayName} because it does not have an IP4 broadcast address.")
            return
        }

    BroadcastReceiver(InetSocketAddress(broadcastAddress, 58765))
    println("Installed a broadcast receiver on $broadcastAddress")
}

class BroadcastReceiver(val address: InetSocketAddress) {
    private val socket = DatagramSocket(address).apply {
        broadcast = true
    }
    private val thread = thread(start = true, name = "broadcast-rx", block = this::listen)
    @Volatile
    private var closed = false

    init {
        Runtime.getRuntime().addShutdownHook(thread(start = false) {
            stop()
        })
    }

    private fun listen() {
        val packet = DatagramPacket(ByteArray(1024), 1024)
        while(true) {
            try {
                socket.receive(packet)
                println("Received a packet of ${packet.length} bytes:\n${format(packet.data, packet.length)}")
                println(String(packet.data, 0, packet.length))
            } catch (ex: InterruptedException) {
                if (closed) {
                    return
                }
            }
        }
    }

    fun stop() {
        closed = true
        thread.interrupt()
    }
}

fun format(data: ByteArray, length: Int, nColumns: Int = 10): String {
    val output = StringBuilder()
    var currentColumn = 0
    for (offset in 0 until length) {
        output.append(data[offset].toUByte().toString(16).uppercase().padStart(2, '0'))
        if (currentColumn < nColumns) {
            output.append(' ')
            currentColumn++
        } else {
            output.append('\n')
            currentColumn = 0
        }
    }

    return output.toString()
}