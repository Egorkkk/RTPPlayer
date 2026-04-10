package com.local.rtpplayer

import android.util.Log
import java.net.BindException
import java.net.DatagramPacket
import java.net.DatagramSocket
import java.net.InetSocketAddress
import java.net.SocketTimeoutException

object UdpDebugProbe {
    private const val TAG = "RtpPlayer_UdpProbe"

    data class Result(
        val received: Boolean,
        val detail: String
    )

    fun runOnce(
        port: Int,
        timeoutMs: Int,
        onComplete: (Result) -> Unit
    ) {
        Thread {
            val result = try {
                DatagramSocket(null).use { socket ->
                    socket.reuseAddress = true
                    socket.soTimeout = timeoutMs
                    socket.bind(InetSocketAddress(port))

                    Log.i(TAG, "UDP probe listening on port=$port timeoutMs=$timeoutMs")

                    val buffer = ByteArray(2048)
                    val packet = DatagramPacket(buffer, buffer.size)
                    socket.receive(packet)

                    Result(
                        received = true,
                        detail = "received len=${packet.length} from=${packet.address?.hostAddress}:${packet.port}"
                    )
                }
            } catch (e: SocketTimeoutException) {
                Result(
                    received = false,
                    detail = "timeout waiting for UDP on port=$port"
                )
            } catch (e: BindException) {
                Result(
                    received = false,
                    detail = "bind failed on port=$port: ${e.message}"
                )
            } catch (e: Exception) {
                Result(
                    received = false,
                    detail = "probe failed: ${e.javaClass.simpleName}: ${e.message}"
                )
            }

            onComplete(result)
        }.start()
    }
}
