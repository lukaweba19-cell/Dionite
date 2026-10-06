// ============================================================================
// Dionite — AudioEngine.
//
// The game ships with zero audio assets: everything you hear is synthesised
// on the fly by an AVAudioSourceNode render block.
//
//   * SFX      — a pool of enveloped oscillator voices; every Sound the C++
//                core posts maps to a recipe (waveform, sweep, noise mix,
//                length) in `recipe(for:)`.
//   * Music    — a generative score driven by the core's MusicMood state
//                machine: scale, tempo, drums and pad change per mood, so
//                combat, boss, death, spire and hub all sound distinct.
//   * Ambience — a filtered-noise bed per biome (wind, embers, deep crypt
//                rumble, ice hiss, sky shimmer) plus chirps and crackle.
//
// Threading: the C++ core queues cues during dionite_tick() on the main
// thread. `pumpFromCore()` drains them once per frame and stages them in a
// short lock-protected FIFO; the render block consumes that FIFO on the
// audio thread. Everything else the render block touches is audio-thread
// only, so the hot path never blocks on the game thread.
// ============================================================================
import AVFoundation
import Darwin
import Foundation

final class AudioEngine {
    static let shared = AudioEngine()

    // MARK: - Shared types

    private struct Cue {
        var id: Int32
        var gain: Float
        var pitch: Float
        var pan: Float
    }

    private enum Wave: Int32 {
        case sine = 0
        case triangle = 1
        case square = 2
        case saw = 3
        case noise = 4
    }

    /// Mirrors dionite::audio::Sound (src/Audio/AudioManager.h). The C++
    /// enum class never crosses the bridging header — raw ints do — so this
    /// table must stay in lock-step with it.
    private enum Snd: Int32 {
        case none = 0
        case gunShot = 1
        case gunShotHeavy = 2
        case swing = 3
        case bowShot = 4
        case reload = 5
        case impact = 6
        case critHit = 7
        case enemyHit = 8
        case enemyDie = 9
        case bossDie = 10
        case bossRoar = 11
        case playerHurt = 12
        case playerDeath = 13
        case castFire = 14
        case castFrost = 15
        case castShock = 16
        case castHoly = 17
        case castShadow = 18
        case dash = 19
        case heal = 20
        case ultimate = 21
        case chestOpen = 22
        case mimicBite = 23
        case goldPickup = 24
        case itemPickup = 25
        case legendaryDrop = 26
        case potionDrink = 27
        case portal = 28
        case doorOpen = 29
        case shrine = 30
        case levelUp = 31
        case questAccept = 32
        case questComplete = 33
        case skillLearn = 34
        case craftSuccess = 35
        case craftFail = 36
        case buyItem = 37
        case sellItem = 38
        case socketGem = 39
        case runeWord = 40
        case uiClick = 41
        case uiError = 42
        case tabSwitch = 43
        case worldEvent = 44
        case thunder = 45
        case spireFloor = 46
        case ghostSpawn = 47
        case victory = 48
    }

    private struct Recipe {
        var wave: Wave = .sine
        var freq: Float = 440
        var freqEnd: Float = 440
        var dur: Float = 0.3
        var gain: Float = 0.5
        var attack: Float = 0.004
        var lfoRate: Float = 0
        var lfoDepth: Float = 0
        var noiseMix: Float = 0
        var lp: Float = 0
    }

    private struct Voice {
        var active = false
        var wave: Wave = .sine
        var freq: Float = 0
        var freqEnd: Float = 0
        var dur: Float = 0
        var age: Float = 0
        var gain: Float = 0
        var attack: Float = 0.004
        var lfoRate: Float = 0
        var lfoDepth: Float = 0
        var noiseMix: Float = 0
        var lpAlpha: Float = 1
        var pitch: Float = 1
        var phase: Float = 0
        var noiseState: Float = 0
        var panL: Float = 0.707
        var panR: Float = 0.707
        var bus: Int32 = 0 // 0 = sfx bus, 1 = music/ambience bus
    }

    private struct MoodDef {
        var root: Float
        var scale: [Float]
        var bpm: Float
        var melodyGain: Float
        var bassGain: Float
        var drums: Bool
        var wave: Wave
    }

    private struct BedParams {
        var lpAlpha: Float = 1
        var droneFreq: Float = 0
        var droneLevel: Float = 0
        var hissLevel: Float = 0
        var crackle: Bool = false
        var chirp: Bool = false
    }

    // MARK: - State

    private let engine = AVAudioEngine()
    private var sourceNode: AVAudioSourceNode?
    private var running = false

    // Shared across threads — always reached under `lock`.
    private let lock = NSLock()
    private var pendingCues: [Cue] = []
    private var sharedMood: Int32 = 0
    private var sharedBed: Int32 = 0
    private var sharedSfxGain: Float = 1
    private var sharedMusicGain: Float = 0.7

    // Audio-thread only.
    private let sampleRate: Double = 44100
    private let maxFrames = 4096
    private let voiceCount = 44
    private var voices: [Voice] = []
    private var scratchL: [Float] = []
    private var scratchR: [Float] = []
    private var rng: UInt32 = 0x1A2B3C4D

    private var musicClock: Float = 0
    private var beatIndex: Int = 0
    private var lastMood: Int32 = -1
    private var musicSeed: UInt32 = 7

    private var bed = BedParams()
    private var activeBed: Int32 = 0
    private var ambLevel: Float = 0
    private var ambPhase: Float = 0
    private var ambLP: Float = 0
    private var ambTime: Float = 0
    private var crackleEnv: Float = 0
    private var chirpTimer: Float = 2

    private init() {
        voices = [Voice](repeating: Voice(), count: voiceCount)
        scratchL = [Float](repeating: 0, count: maxFrames)
        scratchR = [Float](repeating: 0, count: maxFrames)
    }

    // MARK: - Lifecycle

    func start() {
        guard !running else { return }
        do {
            let session = AVAudioSession.sharedInstance()
            try session.setCategory(.playback, mode: .default)
            try session.setActive(true)

            guard let format = AVAudioFormat(standardFormatWithSampleRate: sampleRate,
                                             channels: 2) else { return }
            let node = AVAudioSourceNode(format: format) { [weak self] _, _, frameCount, list -> OSStatus in
                guard let self = self else { return noErr }
                return self.render(frames: Int(frameCount), list: list)
            }
            engine.attach(node)
            engine.connect(node, to: engine.mainMixerNode, format: format)
            engine.mainMixerNode.outputVolume = 1.0
            engine.prepare()
            try engine.start()
            sourceNode = node
            running = true
        } catch {
            running = false
        }
    }

    func pauseEngine() {
        guard running else { return }
        engine.pause()
    }

    func resumeEngine() {
        guard running else { return }
        if !engine.isRunning {
            try? engine.start()
        }
    }

    func stopEngine() {
        guard running else { return }
        engine.stop()
        running = false
    }

    // MARK: - Main-thread pump

    /// Drains the core's cue queue and refreshes the shared score state.
    /// Call once per frame, right after dionite_tick().
    func pumpFromCore() {
        var id: Int32 = 0
        var gain: Float = 0
        var pitch: Float = 0
        var pan: Float = 0
        var drained = 0
        while dionite_audio_poll(&id, &gain, &pitch, &pan) != 0 {
            lock.lock()
            if pendingCues.count < 192 {
                pendingCues.append(Cue(id: id, gain: gain, pitch: pitch, pan: pan))
            }
            lock.unlock()
            drained += 1
            if drained >= 96 { break }
        }

        let mood = dionite_audio_music()
        let bedId = dionite_audio_ambient()
        let sfx = dionite_audio_sfx_gain()
        let music = dionite_audio_music_gain()
        lock.lock()
        sharedMood = mood
        sharedBed = bedId
        sharedSfxGain = sfx
        sharedMusicGain = music
        lock.unlock()
    }

    // MARK: - Mixer (settings UI)

    struct MixerState {
        var master: Float
        var music: Float
        var sfx: Float
        var muted: Bool
    }

    func setMaster(_ value: Float) { dionite_audio_set_master(value) }
    func setMusicVolume(_ value: Float) { dionite_audio_set_music_volume(value) }
    func setSfxVolume(_ value: Float) { dionite_audio_set_sfx_volume(value) }
    func setMuted(_ muted: Bool) { dionite_audio_set_muted(muted ? 1 : 0) }

    /// Reads the core's mixer (reflected after a save restores it).
    func mixerState() -> MixerState {
        return MixerState(master: dionite_audio_master(),
                          music: dionite_audio_music_volume(),
                          sfx: dionite_audio_sfx_volume(),
                          muted: dionite_audio_is_muted() != 0)
    }

    // MARK: - Render (audio thread only)

    private func render(frames inputFrames: Int,
                        list: UnsafeMutablePointer<AudioBufferList>) -> OSStatus {
        let frames = min(inputFrames, maxFrames)

        // 1. Stage: cues + score state under the lock, then never touch it
        //    again for the rest of the block.
        lock.lock()
        let cues = pendingCues
        pendingCues.removeAll(keepingCapacity: true)
        let mood = sharedMood
        let bedId = sharedBed
        let sfxGain = sharedSfxGain
        let musicGain = sharedMusicGain
        lock.unlock()

        for cue in cues {
            spawn(recipe(for: cue.id), pitch: cue.pitch, pan: cue.pan,
                  gain: cue.gain, bus: 0)
        }

        // 2. Generative score.
        let dt = Float(frames) / Float(sampleRate)
        updateMusic(dt: dt, mood: mood)

        // 3. Silence the scratch bus.
        for i in 0..<frames {
            scratchL[i] = 0
            scratchR[i] = 0
        }

        // 4. Voices (sfx scaled by the sfx bus, music notes by the music bus).
        renderVoices(frames: frames, sfxGain: sfxGain, musicGain: musicGain)

        // 5. Ambience bed.
        updateAmbient(dt: dt, bedId: bedId, gain: musicGain, frames: frames)

        // 6. Soft-clip and write out.
        let buffers = UnsafeMutableAudioBufferListPointer(list)
        guard buffers.count > 1,
              let rawL = buffers[0].mData?.assumingMemoryBound(to: Float.self),
              let rawR = buffers[1].mData?.assumingMemoryBound(to: Float.self) else {
            return noErr
        }
        for i in 0..<frames {
            rawL[i] = saturate(scratchL[i])
            rawR[i] = saturate(scratchR[i])
        }
        return noErr
    }

    private func saturate(_ x: Float) -> Float {
        let driven = x * 1.35
        return driven / (1 + 0.42 * abs(driven))
    }

    private func renderVoices(frames: Int, sfxGain: Float, musicGain: Float) {
        let twoPi = Float(2 * Double.pi)
        let step = 1 / Float(sampleRate)
        for i in 0..<voices.count where voices[i].active {
            var voice = voices[i]
            voice.age += step
            if voice.age >= voice.dur {
                voices[i].active = false
                continue
            }

            // Envelope: linear attack, exponential body.
            let atk = max(voice.attack, 0.0005)
            let env: Float
            if voice.age < atk {
                env = voice.age / atk
            } else {
                let x = (voice.age - atk) / max(voice.dur - atk, 0.0002)
                env = expf(-5 * x)
            }

            // Frequency sweep + vibrato.
            let k = min(1, voice.age / max(voice.dur, 0.0001))
            var freq = voice.freq + (voice.freqEnd - voice.freq) * k
            freq *= voice.pitch
            if voice.lfoDepth > 0 {
                freq *= 1 + voice.lfoDepth * sinf(twoPi * voice.lfoRate * voice.age)
            }

            // Oscillator.
            let phase = voice.phase
            var osc: Float
            switch voice.wave {
            case .sine:
                osc = sinf(twoPi * phase)
            case .triangle:
                osc = 1 - 4 * abs(phase - 0.5)
            case .square:
                osc = phase < 0.5 ? 1 : -1
            case .saw:
                osc = 2 * phase - 1
            case .noise:
                osc = whiteNoise()
            }
            voice.phase = phase + freq * step
            if voice.phase >= 1 { voice.phase -= 1 }

            var sample = osc * (1 - voice.noiseMix)
            if voice.noiseMix > 0 {
                var n = whiteNoise()
                if voice.lpAlpha < 1 {
                    voice.noiseState += (n - voice.noiseState) * voice.lpAlpha
                    n = voice.noiseState
                }
                sample += n * voice.noiseMix
            }

            let busGain = voice.bus == 0 ? sfxGain : musicGain
            let value = sample * env * voice.gain * busGain
            scratchL[i] += value * voice.panL
            scratchR[i] += value * voice.panR
            voices[i] = voice
        }
    }

    // MARK: - Voices

    private func spawn(_ recipe: Recipe, pitch: Float, pan: Float,
                       gain: Float, bus: Int32) {
        var slot = -1
        var oldest = -1
        var oldestAge: Float = -1
        for i in 0..<voices.count where !voices[i].active {
            slot = i
            break
        }
        if slot < 0 {
            for i in 0..<voices.count where voices[i].age > oldestAge {
                oldestAge = voices[i].age
                oldest = i
            }
            slot = oldest
        }
        guard slot >= 0 else { return }

        let angle = (max(-1, min(1, pan)) + 1) * Float.pi / 4
        var voice = Voice()
        voice.active = true
        voice.wave = recipe.wave
        voice.freq = recipe.freq
        voice.freqEnd = recipe.freqEnd
        voice.dur = max(recipe.dur, 0.02)
        voice.age = 0
        voice.gain = max(0, min(1, gain)) * recipe.gain
        voice.attack = recipe.attack
        voice.lfoRate = recipe.lfoRate
        voice.lfoDepth = recipe.lfoDepth
        voice.noiseMix = recipe.noiseMix
        voice.lpAlpha = recipe.lp > 0 ? noiseAlpha(recipe.lp) : 1
        voice.pitch = max(0.5, min(2, pitch))
        voice.phase = 0
        voice.noiseState = 0
        voice.panL = cosf(angle)
        voice.panR = sinf(angle)
        voice.bus = bus
        voices[slot] = voice
    }

    private func noiseAlpha(_ cutoff: Float) -> Float {
        let f = max(20, min(cutoff, Float(sampleRate) * 0.45))
        return 1 - expf(-2 * Float.pi * f / Float(sampleRate))
    }

    // MARK: - Random

    private func nextRandom() -> UInt32 {
        rng ^= rng << 13
        rng ^= rng >> 17
        rng ^= rng << 5
        return rng
    }

    /// -1 .. +1
    private func whiteNoise() -> Float {
        return Float(nextRandom() & 0xFFFF) / 32767.5 - 1
    }

    /// 0 .. 1
    private func unitRandom() -> Float {
        return Float(nextRandom() & 0xFFFFFF) / 16_777_215
    }

    // MARK: - Generative music

    private func moodDef(_ mood: Int32) -> MoodDef {
        switch mood {
        case 1: // Hub — warm, safe, slow
            return MoodDef(root: 220, scale: [0, 2, 4, 7, 9], bpm: 66,
                           melodyGain: 0.15, bassGain: 0.09, drums: false, wave: .sine)
        case 2: // Explore — wandering, uneasy
            return MoodDef(root: 146.83, scale: [0, 2, 3, 5, 7, 10], bpm: 58,
                           melodyGain: 0.11, bassGain: 0.07, drums: false, wave: .triangle)
        case 3: // Combat — driving
            return MoodDef(root: 82.41, scale: [0, 2, 3, 5, 7, 8, 10], bpm: 126,
                           melodyGain: 0.17, bassGain: 0.15, drums: true, wave: .saw)
        case 4: // Boss — oppressive, phrygian
            return MoodDef(root: 65.41, scale: [0, 1, 3, 5, 7, 8, 10], bpm: 100,
                           melodyGain: 0.19, bassGain: 0.18, drums: true, wave: .saw)
        case 5: // Death — dirge
            return MoodDef(root: 87.31, scale: [0, 3, 5, 7], bpm: 48,
                           melodyGain: 0.10, bassGain: 0.10, drums: false, wave: .sine)
        case 6: // Spire — dissonant climb
            return MoodDef(root: 98, scale: [0, 1, 4, 5, 6, 7, 11], bpm: 132,
                           melodyGain: 0.16, bassGain: 0.15, drums: true, wave: .square)
        case 7: // Victory — release
            return MoodDef(root: 261.63, scale: [0, 2, 4, 5, 7, 9, 11], bpm: 96,
                           melodyGain: 0.20, bassGain: 0.11, drums: false, wave: .triangle)
        default: // Silent
            return MoodDef(root: 0, scale: [], bpm: 60,
                           melodyGain: 0, bassGain: 0, drums: false, wave: .sine)
        }
    }

    private func updateMusic(dt: Float, mood: Int32) {
        guard mood > 0 else {
            lastMood = mood
            return
        }
        let def = moodDef(mood)
        if mood != lastMood {
            lastMood = mood
            musicClock = 0
            beatIndex = 0
            musicSeed = UInt32(bitPattern: mood) &* 2_654_435_761 &+ 0x9E3779B9
            rng = musicSeed
        }

        let beatLen = 60 / max(def.bpm, 1)
        musicClock += dt
        var guardCount = 0
        while musicClock >= beatLen && guardCount < 8 {
            musicClock -= beatLen
            musicBeat(def: def, beat: beatIndex, beatLen: beatLen)
            beatIndex += 1
            guardCount += 1
        }
    }

    private func musicBeat(def: MoodDef, beat: Int, beatLen: Float) {
        guard !def.scale.isEmpty else { return }
        let slot = beat % 4

        if def.drums {
            if slot == 0 || slot == 2 {
                var kick = Recipe(wave: .sine, freq: 130, freqEnd: 42,
                                  dur: 0.19, gain: 0.55, attack: 0.002)
                kick.noiseMix = 0.12
                spawn(kick, pitch: 1, pan: 0, gain: 1, bus: 1)
            }
            if slot == 1 || slot == 3 {
                var hat = Recipe(wave: .noise, freq: 0, freqEnd: 0,
                                 dur: 0.045, gain: 0.09, attack: 0.001)
                hat.lp = 7000
                spawn(hat, pitch: 1, pan: 0.2, gain: 1, bus: 1)
            }
        }

        if slot == 0 {
            let bass = Recipe(wave: .saw, freq: def.root, freqEnd: def.root,
                              dur: beatLen * 2.4, gain: def.bassGain, attack: 0.012)
            spawn(bass, pitch: 1, pan: 0, gain: 1, bus: 1)

            let pad = Recipe(wave: .sine, freq: def.root * 2, freqEnd: def.root * 2,
                             dur: beatLen * 4.6, gain: def.melodyGain * 0.55,
                             attack: beatLen * 0.8, lfoRate: 0.15, lfoDepth: 0.12)
            spawn(pad, pitch: 1, pan: -0.25, gain: 1, bus: 1)
            let padHi = Recipe(wave: .sine, freq: def.root * 3, freqEnd: def.root * 3,
                               dur: beatLen * 4.6, gain: def.melodyGain * 0.35,
                               attack: beatLen * 0.9, lfoRate: 0.21, lfoDepth: 0.1)
            spawn(padHi, pitch: 1, pan: 0.25, gain: 1, bus: 1)
        }

        if beat % 2 == 0 {
            let index = Int(nextRandom() % UInt32(def.scale.count))
            var semis = def.scale[index]
            if nextRandom() % 4 == 0 { semis += 12 }
            let freq = def.root * 4 * powf(2, semis / 12)
            let note = Recipe(wave: def.wave, freq: freq, freqEnd: freq,
                              dur: beatLen * 1.9, gain: def.melodyGain,
                              attack: 0.006, lfoRate: 5.2, lfoDepth: 0.006)
            let pan: Float = nextRandom() % 2 == 0 ? -0.35 : 0.35
            spawn(note, pitch: 1, pan: pan, gain: 1, bus: 1)
        }
    }

    // MARK: - Ambience

    private func refreshBed(_ bedId: Int32) {
        guard bedId != activeBed else { return }
        activeBed = bedId
        switch bedId {
        case 1: // Forest — breezy wind through leaves, birdsong
            bed = BedParams(lpAlpha: noiseAlpha(950), droneFreq: 92,
                            droneLevel: 0.030, hissLevel: 0.055,
                            crackle: false, chirp: true)
        case 2: // Ashen wastes — low smoulder, embers popping
            bed = BedParams(lpAlpha: noiseAlpha(620), droneFreq: 62,
                            droneLevel: 0.055, hissLevel: 0.055,
                            crackle: true, chirp: false)
        case 3: // Crypts — deep wet rumble
            bed = BedParams(lpAlpha: noiseAlpha(260), droneFreq: 55,
                            droneLevel: 0.075, hissLevel: 0.028,
                            crackle: false, chirp: false)
        case 4: // Ice — thin, cutting wind
            bed = BedParams(lpAlpha: noiseAlpha(5200), droneFreq: 130,
                            droneLevel: 0.018, hissLevel: 0.085,
                            crackle: false, chirp: false)
        case 5: // Sky — airy shimmer
            bed = BedParams(lpAlpha: noiseAlpha(3000), droneFreq: 196,
                            droneLevel: 0.028, hissLevel: 0.065,
                            crackle: false, chirp: false)
        default:
            bed = BedParams()
        }
    }

    private func updateAmbient(dt: Float, bedId: Int32, gain: Float, frames: Int) {
        refreshBed(bedId)
        let target: Float = bedId > 0 && gain > 0.01 ? 1 : 0
        ambLevel += (target - ambLevel) * min(1, dt * 1.6)
        if ambLevel < 0.002 && target == 0 { return }
        guard gain > 0.001 else { return }

        if bed.chirp {
            chirpTimer -= dt
            if chirpTimer <= 0 {
                chirpTimer = 2.4 + unitRandom() * 3.6
                let base = 2100 + unitRandom() * 700
                let chirp = Recipe(wave: .sine, freq: base, freqEnd: base * 1.32,
                                   dur: 0.11, gain: 0.05, attack: 0.01,
                                   lfoRate: 18, lfoDepth: 0.25)
                spawn(chirp, pitch: 1, pan: unitRandom(), gain: ambLevel, bus: 1)
            }
        }

        let twoPi = Float(2 * Double.pi)
        let step = 1 / Float(sampleRate)
        let droneStep = twoPi * bed.droneFreq * step
        for i in 0..<frames {
            ambTime += step
            ambPhase += droneStep
            if ambPhase > twoPi { ambPhase -= twoPi }

            var n = whiteNoise()
            ambLP += (n - ambLP) * bed.lpAlpha
            var sample = ambLP * bed.hissLevel
            sample += sinf(ambPhase) * bed.droneLevel

            if bed.crackle {
                if unitRandom() < 0.0004 { crackleEnv = 0.9 }
                if crackleEnv > 0.001 {
                    n = whiteNoise()
                    sample += n * crackleEnv * 0.5
                    crackleEnv *= 0.9992
                }
            }

            let gust = 1 + 0.4 * sinf(twoPi * 0.06 * ambTime)
            let value = sample * ambLevel * gust * gain * 0.8
            scratchL[i] += value * 0.8
            scratchR[i] += value * 0.8
        }
    }

    // MARK: - SFX recipes

    private func recipe(for id: Int32) -> Recipe {
        guard let sound = Snd(rawValue: id) else { return Recipe() }
        switch sound {
        case .none:
            return Recipe(dur: 0.01, gain: 0)
        case .gunShot:
            var r = Recipe(wave: .square, freq: 320, freqEnd: 90,
                           dur: 0.13, gain: 0.62, attack: 0.001)
            r.noiseMix = 0.75
            r.lp = 2600
            return r
        case .gunShotHeavy:
            var r = Recipe(wave: .square, freq: 170, freqEnd: 48,
                           dur: 0.28, gain: 0.8, attack: 0.001)
            r.noiseMix = 0.8
            r.lp = 1500
            return r
        case .swing:
            var r = Recipe(wave: .noise, freq: 0, freqEnd: 0,
                           dur: 0.22, gain: 0.3, attack: 0.03)
            r.lp = 3400
            return r
        case .bowShot:
            var r = Recipe(wave: .triangle, freq: 980, freqEnd: 300,
                           dur: 0.12, gain: 0.5, attack: 0.001)
            r.noiseMix = 0.2
            return r
        case .reload:
            var r = Recipe(wave: .noise, freq: 0, freqEnd: 0,
                           dur: 0.05, gain: 0.42, attack: 0.001)
            r.lp = 5200
            return r
        case .impact:
            var r = Recipe(wave: .noise, freq: 0, freqEnd: 0,
                           dur: 0.09, gain: 0.4, attack: 0.001)
            r.lp = 1400
            return r
        case .critHit:
            var r = Recipe(wave: .square, freq: 1500, freqEnd: 420,
                           dur: 0.13, gain: 0.6, attack: 0.001)
            r.noiseMix = 0.35
            r.lp = 4200
            return r
        case .enemyHit:
            var r = Recipe(wave: .noise, freq: 0, freqEnd: 0,
                           dur: 0.07, gain: 0.38, attack: 0.001)
            r.lp = 2200
            return r
        case .enemyDie:
            var r = Recipe(wave: .saw, freq: 430, freqEnd: 80,
                           dur: 0.4, gain: 0.5, attack: 0.004)
            r.noiseMix = 0.3
            r.lp = 1800
            return r
        case .bossDie:
            var r = Recipe(wave: .saw, freq: 210, freqEnd: 36,
                           dur: 1.3, gain: 0.85, attack: 0.01,
                           lfoRate: 4, lfoDepth: 0.05)
            r.noiseMix = 0.4
            r.lp = 1200
            return r
        case .bossRoar:
            var r = Recipe(wave: .saw, freq: 96, freqEnd: 66,
                           dur: 1.0, gain: 0.85, attack: 0.05,
                           lfoRate: 7, lfoDepth: 0.09)
            r.noiseMix = 0.45
            r.lp = 900
            return r
        case .playerHurt:
            var r = Recipe(wave: .square, freq: 340, freqEnd: 150,
                           dur: 0.2, gain: 0.55, attack: 0.002)
            r.noiseMix = 0.25
            return r
        case .playerDeath:
            var r = Recipe(wave: .saw, freq: 620, freqEnd: 70,
                           dur: 1.6, gain: 0.7, attack: 0.01,
                           lfoRate: 3, lfoDepth: 0.03)
            r.lp = 2000
            return r
        case .castFire:
            var r = Recipe(wave: .saw, freq: 260, freqEnd: 720,
                           dur: 0.34, gain: 0.5, attack: 0.01)
            r.noiseMix = 0.5
            r.lp = 2400
            return r
        case .castFrost:
            var r = Recipe(wave: .triangle, freq: 1500, freqEnd: 2400,
                           dur: 0.4, gain: 0.42, attack: 0.01,
                           lfoRate: 9, lfoDepth: 0.12)
            r.noiseMix = 0.2
            return r
        case .castShock:
            var r = Recipe(wave: .square, freq: 1200, freqEnd: 800,
                           dur: 0.24, gain: 0.48, attack: 0.001,
                           lfoRate: 42, lfoDepth: 0.5)
            r.noiseMix = 0.3
            return r
        case .castHoly:
            var r = Recipe(wave: .sine, freq: 880, freqEnd: 1320,
                           dur: 0.5, gain: 0.5, attack: 0.01,
                           lfoRate: 5, lfoDepth: 0.02)
            return r
        case .castShadow:
            var r = Recipe(wave: .saw, freq: 240, freqEnd: 90,
                           dur: 0.5, gain: 0.5, attack: 0.02,
                           lfoRate: 6, lfoDepth: 0.08)
            r.lp = 1600
            return r
        case .dash:
            var r = Recipe(wave: .noise, freq: 0, freqEnd: 0,
                           dur: 0.18, gain: 0.34, attack: 0.01)
            r.lp = 4800
            return r
        case .heal:
            var r = Recipe(wave: .sine, freq: 660, freqEnd: 990,
                           dur: 0.5, gain: 0.45, attack: 0.02,
                           lfoRate: 4.5, lfoDepth: 0.02)
            return r
        case .ultimate:
            var r = Recipe(wave: .saw, freq: 110, freqEnd: 440,
                           dur: 1.2, gain: 0.75, attack: 0.02,
                           lfoRate: 8, lfoDepth: 0.06)
            r.noiseMix = 0.35
            r.lp = 2000
            return r
        case .chestOpen:
            var r = Recipe(wave: .triangle, freq: 520, freqEnd: 780,
                           dur: 0.3, gain: 0.5, attack: 0.003)
            r.noiseMix = 0.3
            r.lp = 3000
            return r
        case .mimicBite:
            var r = Recipe(wave: .saw, freq: 160, freqEnd: 55,
                           dur: 0.35, gain: 0.7, attack: 0.002)
            r.noiseMix = 0.5
            r.lp = 1600
            return r
        case .goldPickup:
            return Recipe(wave: .sine, freq: 1320, freqEnd: 1760,
                          dur: 0.09, gain: 0.3, attack: 0.002)
        case .itemPickup:
            return Recipe(wave: .triangle, freq: 880, freqEnd: 1175,
                          dur: 0.15, gain: 0.35, attack: 0.003)
        case .legendaryDrop:
            var r = Recipe(wave: .saw, freq: 523, freqEnd: 1046,
                           dur: 0.75, gain: 0.55, attack: 0.01,
                           lfoRate: 6, lfoDepth: 0.03)
            r.noiseMix = 0.15
            return r
        case .potionDrink:
            var r = Recipe(wave: .noise, freq: 0, freqEnd: 0,
                           dur: 0.3, gain: 0.4, attack: 0.02)
            r.lp = 900
            return r
        case .portal:
            var r = Recipe(wave: .sine, freq: 200, freqEnd: 820,
                           dur: 0.65, gain: 0.5, attack: 0.03,
                           lfoRate: 7, lfoDepth: 0.06)
            r.noiseMix = 0.15
            return r
        case .doorOpen:
            var r = Recipe(wave: .saw, freq: 130, freqEnd: 105,
                           dur: 0.5, gain: 0.35, attack: 0.06,
                           lfoRate: 5, lfoDepth: 0.15)
            r.lp = 1200
            return r
        case .shrine:
            var r = Recipe(wave: .sine, freq: 440, freqEnd: 554,
                           dur: 0.9, gain: 0.4, attack: 0.06,
                           lfoRate: 3.5, lfoDepth: 0.02)
            return r
        case .levelUp:
            var r = Recipe(wave: .triangle, freq: 440, freqEnd: 1320,
                           dur: 0.9, gain: 0.6, attack: 0.005,
                           lfoRate: 5, lfoDepth: 0.02)
            r.noiseMix = 0.1
            return r
        case .questAccept:
            return Recipe(wave: .sine, freq: 660, freqEnd: 880,
                          dur: 0.25, gain: 0.4, attack: 0.005)
        case .questComplete:
            var r = Recipe(wave: .triangle, freq: 880, freqEnd: 1320,
                           dur: 0.45, gain: 0.5, attack: 0.005)
            r.noiseMix = 0.08
            return r
        case .skillLearn:
            return Recipe(wave: .triangle, freq: 990, freqEnd: 990,
                          dur: 0.3, gain: 0.4, attack: 0.004)
        case .craftSuccess:
            return Recipe(wave: .saw, freq: 520, freqEnd: 780,
                          dur: 0.35, gain: 0.42, attack: 0.005)
        case .craftFail:
            return Recipe(wave: .square, freq: 220, freqEnd: 140,
                          dur: 0.4, gain: 0.4, attack: 0.006)
        case .buyItem:
            return Recipe(wave: .sine, freq: 1568, freqEnd: 1568,
                          dur: 0.12, gain: 0.35, attack: 0.002)
        case .sellItem:
            return Recipe(wave: .sine, freq: 1046, freqEnd: 1046,
                          dur: 0.12, gain: 0.35, attack: 0.002)
        case .socketGem:
            return Recipe(wave: .sine, freq: 1318, freqEnd: 1760,
                          dur: 0.16, gain: 0.4, attack: 0.002)
        case .runeWord:
            var r = Recipe(wave: .sine, freq: 392, freqEnd: 784,
                           dur: 0.8, gain: 0.45, attack: 0.02,
                           lfoRate: 5, lfoDepth: 0.03)
            r.noiseMix = 0.1
            return r
        case .uiClick:
            return Recipe(wave: .sine, freq: 1000, freqEnd: 1000,
                          dur: 0.05, gain: 0.25, attack: 0.001)
        case .uiError:
            return Recipe(wave: .square, freq: 220, freqEnd: 190,
                          dur: 0.18, gain: 0.3, attack: 0.002)
        case .tabSwitch:
            return Recipe(wave: .sine, freq: 700, freqEnd: 700,
                          dur: 0.06, gain: 0.18, attack: 0.001)
        case .worldEvent:
            var r = Recipe(wave: .saw, freq: 196, freqEnd: 294,
                           dur: 0.9, gain: 0.5, attack: 0.08,
                           lfoRate: 3, lfoDepth: 0.04)
            r.lp = 2200
            return r
        case .thunder:
            var r = Recipe(wave: .noise, freq: 0, freqEnd: 0,
                           dur: 1.5, gain: 0.8, attack: 0.01)
            r.lp = 420
            return r
        case .spireFloor:
            var r = Recipe(wave: .saw, freq: 150, freqEnd: 74,
                           dur: 1.0, gain: 0.6, attack: 0.02,
                           lfoRate: 5, lfoDepth: 0.07)
            r.lp = 1400
            return r
        case .ghostSpawn:
            var r = Recipe(wave: .noise, freq: 0, freqEnd: 0,
                           dur: 0.6, gain: 0.4, attack: 0.12)
            r.lp = 2600
            return r
        case .victory:
            var r = Recipe(wave: .triangle, freq: 523, freqEnd: 1046,
                           dur: 1.6, gain: 0.6, attack: 0.01,
                           lfoRate: 4, lfoDepth: 0.02)
            r.noiseMix = 0.1
            return r
        }
    }
}
