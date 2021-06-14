// Copyright AudioKit. All Rights Reserved. Revision History at http://github.com/AudioKit/AudioKit/

import AVFoundation
import CMicrophonePitchDetector

/// Tap to do pitch tracking on any node.
/// start() will add the tap, and stop() will remove it.
final class PitchTap {
    /// Size of buffer to analyze
    private(set) var bufferSize: UInt32

    /// Tells whether the node is processing (ie. started, playing, or active)
    private(set) var isStarted: Bool = false

    /// The bus to install the tap onto
    var bus: Int = 0 {
        didSet {
            if isStarted {
                stop()
                start()
            }
        }
    }

    private var _input: Node

    /// Input node to analyze
    var input: Node {
        get {
            return _input
        }
        set {
            guard newValue !== _input else { return }
            let wasStarted = isStarted

            // if the input changes while it's on, stop and start the tap
            if wasStarted {
                stop()
            }

            _input = newValue

            // if the input changes while it's on, stop and start the tap
            if wasStarted {
                start()
            }
        }
    }

    /// Enable the tap on input
    func start() {
        lock()
        defer {
            unlock()
        }
        guard !isStarted else { return }
        isStarted = true

        // a node can only have one tap at a time installed on it
        // make sure any previous tap is removed.
        // We're making the assumption that the previous tap (if any)
        // was installed on the same bus as our bus var.
        removeTap()

        // just double check this here
        guard input.avAudioNode.engine != nil else {
            Log("The tapped node isn't attached to the engine")
            return
        }

        input.avAudioNode.installTap(onBus: bus,
                                     bufferSize: bufferSize,
                                     format: nil,
                                     block: handleTapBlock(buffer:at:))
    }

    /// Overide this method to handle Tap in derived class
    /// - Parameters:
    ///   - buffer: Buffer to analyze
    ///   - time: Unused in this case
    private func handleTapBlock(buffer: AVAudioPCMBuffer, at time: AVAudioTime) {
        // Call on the main thread so the client doesn't have to worry
        // about thread safety.
        buffer.frameLength = bufferSize
        DispatchQueue.main.async {
            // Create trackers as needed.
            self.lock()
            guard self.isStarted == true else {
                self.unlock()
                return
            }
            self.doHandleTapBlock(buffer: buffer, at: time)
            self.unlock()
        }
    }

    private func removeTap() {
        guard input.avAudioNode.engine != nil else {
            Log("The tapped node isn't attached to the engine")
            return
        }
        input.avAudioNode.removeTap(onBus: bus)
    }

    /// remove the tap and nil out the input reference
    /// this is important in regard to retain cycles on your input node
    func dispose() {
        if isStarted {
            stop()
        }
    }

    private var unfairLock = os_unfair_lock_s()
    func lock() {
        os_unfair_lock_lock(&unfairLock)
    }

    func unlock() {
        os_unfair_lock_unlock(&unfairLock)
    }

    private var pitch: [Float] = [0, 0]
    private var amp: [Float] = [0, 0]
    private var trackers: [PitchTrackerRef] = []

    /// Callback type
    typealias Handler = ([Float], [Float]) -> Void

    private var handler: Handler = { _, _ in }

    /// Initialize the pitch tap
    ///
    /// - Parameters:
    ///   - input: Node to analyze
    ///   - bufferSize: Size of buffer to analyze
    ///   - handler: Callback to call on each analysis pass
    init(_ input: Node, bufferSize: UInt32 = 4_096, handler: @escaping Handler) {
        self.handler = handler
        self.bufferSize = bufferSize
        self._input = input
    }

    deinit {
        for tracker in trackers {
            ztPitchTrackerDestroy(tracker)
        }
    }

    /// Stop detecting pitch
    func stop() {
        lock()
        removeTap()
        isStarted = false
        unlock()
        for i in 0 ..< pitch.count {
            pitch[i] = 0.0
        }
    }

    func doHandleTapBlock(buffer: AVAudioPCMBuffer, at time: AVAudioTime) {
        guard let floatData = buffer.floatChannelData else { return }
        let channelCount = Int(buffer.format.channelCount)
        let length = UInt(buffer.frameLength)
        while self.trackers.count < channelCount {
            self.trackers.append(ztPitchTrackerCreate(UInt32(buffer.format.sampleRate), 4_096, 20))
        }

        while self.amp.count < channelCount {
            self.amp.append(0)
            self.pitch.append(0)
        }

        // n is the channel
        for n in 0 ..< channelCount {
            let data = floatData[n]

            ztPitchTrackerAnalyze(self.trackers[n], data, UInt32(length))

            var a: Float = 0
            var f: Float = 0
            ztPitchTrackerGetResults(self.trackers[n], &a, &f)
            self.amp[n] = a
            self.pitch[n] = f
        }
        self.handler(self.pitch, self.amp)
    }
}
