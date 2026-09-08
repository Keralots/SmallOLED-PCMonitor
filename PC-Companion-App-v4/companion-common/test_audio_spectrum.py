"""Auto-start decisions: arming delay, short-sound rejection, quiet release."""
import unittest
import threading
from contextlib import contextmanager
from unittest.mock import Mock, patch

import audio_spectrum as audio


def trigger(threshold=-45.0, start=3.0, stop=20.0):
    t = audio.VizAutoTrigger()
    t.configure(True, threshold, start, stop)
    return t


def feed_span(t, level_db, t0, seconds, step=0.04):
    """Play one level for a span; returns the actions in order."""
    actions, now = [], t0
    end = t0 + seconds
    while now < end:
        a = t.feed(level_db, now)
        if a:
            actions.append((a, now))
        now += step
    return actions, now


class AutoTriggerTests(unittest.TestCase):
    def test_disabled_never_acts(self):
        t = audio.VizAutoTrigger()
        actions, _ = feed_span(t, -10.0, 0.0, 30.0)
        self.assertEqual(actions, [])

    def test_music_starts_after_delay(self):
        t = trigger(start=3.0)
        actions, _ = feed_span(t, -20.0, 0.0, 5.0)
        self.assertEqual([a for a, _ in actions], ["viz"])
        self.assertAlmostEqual(actions[0][1], 3.0, delta=0.05)
        self.assertTrue(t.forced)

    def test_short_notification_is_ignored(self):
        t = trigger(start=3.0)
        now = 0.0
        for _ in range(4):  # four 1s pops, 2s apart
            _, now = feed_span(t, -15.0, now, 1.0)
            actions, now = feed_span(t, -90.0, now, 2.0)
            self.assertEqual(actions, [])
        self.assertFalse(t.forced)

    def test_short_gap_does_not_rearm(self):
        t = trigger(start=3.0)
        actions, now = feed_span(t, -20.0, 0.0, 2.0)
        self.assertEqual(actions, [])
        _, now = feed_span(t, -90.0, now, 0.5)      # gap below AUTO_GAP_S
        actions, _ = feed_span(t, -20.0, now, 2.0)
        self.assertEqual([a for a, _ in actions], ["viz"])

    def test_release_after_stop_delay_only(self):
        t = trigger(start=1.0, stop=10.0)
        _, now = feed_span(t, -20.0, 0.0, 2.0)
        self.assertTrue(t.forced)
        actions, now = feed_span(t, -90.0, now, 8.0)
        self.assertEqual(actions, [])               # still holding the display
        actions, _ = feed_span(t, -90.0, now, 4.0)
        self.assertEqual([a for a, _ in actions], ["auto"])
        self.assertFalse(t.forced)

    def test_threshold_rejects_quiet_sound(self):
        t = trigger(threshold=-30.0, start=1.0)
        actions, _ = feed_span(t, -40.0, 0.0, 10.0)
        self.assertEqual(actions, [])

    def test_disabling_hands_the_display_back(self):
        t = trigger(start=1.0)
        feed_span(t, -20.0, 0.0, 2.0)
        self.assertTrue(t.configure(False, -45.0, 3.0, 20.0))
        self.assertFalse(t.forced)
        self.assertFalse(t.configure(False, -45.0, 3.0, 20.0))

    def test_settings_are_clamped(self):
        t = trigger()
        t.configure(True, -200.0, -5.0, 0.0)
        self.assertEqual((t.threshold_db, t.start_delay, t.stop_delay),
                         (-80.0, 0.0, 1.0))
        t.configure(True, 50.0, 900.0, 99999.0)
        self.assertEqual((t.threshold_db, t.start_delay, t.stop_delay),
                         (-10.0, 60.0, 3600.0))

    def test_auto_settings_from_config(self):
        self.assertEqual(audio.auto_settings({}),
                         (False, audio.AUTO_THRESHOLD_DB,
                          audio.AUTO_START_DELAY, audio.AUTO_STOP_DELAY))
        self.assertEqual(audio.auto_settings({
            "audio_viz_auto": True, "audio_viz_threshold": -55,
            "audio_viz_start_delay": 5, "audio_viz_stop_delay": 90}),
            (True, -55.0, 5.0, 90.0))


class PacerTests(unittest.TestCase):
    def test_short_gaps_hold_the_last_frame(self):
        self.assertEqual(audio.frame_action(0.0), "hold")
        self.assertEqual(audio.frame_action(audio.HOLD_S), "hold")

    def test_longer_gaps_fade_the_last_frame(self):
        self.assertEqual(audio.frame_action(audio.HOLD_S + 0.01), "decay")
        self.assertEqual(audio.frame_action(audio.GIVE_UP_S - 0.01), "decay")

    def test_dead_capture_stops_sending(self):
        self.assertEqual(audio.frame_action(audio.GIVE_UP_S), "stop")
        self.assertEqual(audio.frame_action(60.0), "stop")

    def test_decay_reaches_silence_before_giving_up(self):
        level = 255.0
        ticks = int((audio.GIVE_UP_S - audio.HOLD_S) / audio.GAP_S)
        for _ in range(ticks):
            level *= audio.STALL_DECAY
        self.assertLess(level, 1.0)


@unittest.skipUnless(audio.AVAILABLE, "Audio dependencies unavailable")
class StreamTimingTests(unittest.TestCase):
    def setUp(self):
        self.stream = audio.SpectrumStreamer("pixelclock.local", 4210)
        self.stream._sock.close()
        self.stream._sock = Mock()
        self.bands = audio.np.zeros(audio.BANDS, dtype=audio.np.uint8)

    def test_capture_sends_only_to_cached_numeric_address(self):
        with patch.object(audio.socket, "getaddrinfo", side_effect=AssertionError("DNS in capture")):
            self.stream._send_bands(self.bands)
            self.stream._sock.sendto.assert_not_called()
            self.stream._target = ("192.0.2.1", 4210)
            self.stream._send_bands(self.bands)
        flat = bytes([128]) * audio.WAVE_POINTS
        self.stream._sock.sendto.assert_called_once_with(
            b"FFT1" + bytes(32) + flat, ("192.0.2.1", 4210))

    def test_packet_carries_bands_then_waveform(self):
        self.stream._target = ("192.0.2.1", 4210)
        wave = audio.np.arange(audio.WAVE_POINTS, dtype=audio.np.uint8)
        self.stream._send_bands(self.bands, wave)
        packet = self.stream._sock.sendto.call_args[0][0]
        self.assertEqual(len(packet), 4 + audio.BANDS + audio.WAVE_POINTS)
        self.assertEqual(packet[:4], b"FFT1")
        self.assertEqual(packet[4 + audio.BANDS:], wave.tobytes())

    def test_waveform_triggers_on_a_rising_zero_crossing(self):
        # Two blocks of the same tone at different phases must yield the same
        # trace, otherwise the scope slides sideways instead of standing still.
        t = audio.np.arange(audio.FRAMES, dtype=audio.np.float32) / audio.RATE
        first = audio.np.sin(2 * audio.np.pi * 110.0 * t).astype(audio.np.float32)
        shifted = audio.np.sin(2 * audio.np.pi * 110.0 * t + 1.1).astype(audio.np.float32)
        a = self.stream._process_wave(first)
        b = self.stream._process_wave(shifted)
        self.assertEqual(len(a), audio.WAVE_POINTS)
        self.assertLess(int(audio.np.abs(a.astype(int) - b.astype(int)).max()), 12)
        self.assertGreater(int(a.max()), 200)
        self.assertLess(int(a.min()), 55)

    def test_silence_stays_a_flat_trace(self):
        quiet = audio.np.zeros(audio.FRAMES, dtype=audio.np.float32)
        wave = self.stream._process_wave(quiet)
        self.assertTrue(bool((wave == 128).all()))

    def test_slow_dns_does_not_block_capture_or_publish_old_target(self):
        entered, finish = threading.Event(), threading.Event()

        def resolve(*args):
            entered.set()
            finish.wait(2)
            self.stream.stop()
            return [(None, None, None, None, ("192.0.2.1", 4210))]

        self.stream._target = ("192.0.2.1", 4210)
        with patch.object(audio, "audio_thread_com"), patch.object(audio.sc, "default_speaker"), \
                patch.object(audio.socket, "getaddrinfo", side_effect=resolve):
            worker = threading.Thread(target=self.stream._maintenance)
            worker.start()
            try:
                self.assertTrue(entered.wait(1))
                sender = threading.Thread(target=self.stream._send_bands, args=(self.bands,))
                sender.start()
                sender.join(timeout=0.5)
                self.assertFalse(sender.is_alive(), "DNS holds up audio sends")
                self.stream.set_target("new-clock.local", 4210)
                finish.set()
                worker.join(timeout=1)
                self.assertIsNone(self.stream._target)
            finally:
                finish.set()
                self.stream.stop()
                worker.join(timeout=2)

    def test_unchanged_settings_keep_resolved_address(self):
        self.stream._target = ("192.0.2.1", 4210)
        self.stream.set_target("pixelclock.local", 4210)
        self.assertEqual(self.stream._target, ("192.0.2.1", 4210))
        self.stream.set_target("pixelclock.local", 4211)
        self.assertIsNone(self.stream._target)

    def test_capture_initializes_com_on_its_own_thread_and_can_join(self):
        entered = []
        exited = []

        @contextmanager
        def com():
            entered.append(threading.get_ident())
            try:
                yield
            finally:
                exited.append(threading.get_ident())

        def capture():
            self.assertIn(threading.get_ident(), entered)

        with patch.object(audio, "audio_thread_com", com), \
                patch.object(audio, "boost_thread_priority"), \
                patch.object(audio, "release_thread_priority"), \
                patch.object(self.stream, "_watchdog"), \
                patch.object(self.stream, "_maintenance"), \
                patch.object(self.stream, "_capture_loop", side_effect=capture):
            self.stream.start()
            self.stream.join(timeout=2)
        self.assertFalse(self.stream.is_alive())
        self.assertEqual(entered, [self.stream.ident])
        self.assertEqual(exited, entered)

    def test_stopped_stream_does_not_send_more_packets(self):
        self.stream._target = ("192.0.2.1", 4210)
        self.stream.stop()
        self.stream._send_bands(self.bands)
        self.stream._sock.sendto.assert_not_called()

    def test_mode_retries_network_failure_using_cached_address(self):
        self.stream._target = ("192.0.2.1", 4210)
        reply = Mock()
        reply.__enter__ = Mock(return_value=reply)
        reply.__exit__ = Mock(return_value=False)
        with patch.object(audio, "urlopen", side_effect=[OSError("network waking"), reply]) as request, \
                patch.object(audio.time, "monotonic", return_value=10.0) as now:
            self.stream._send_mode("auto")
            request.assert_not_called()  # Capture only queues the request.
            self.stream._flush_mode()
            self.assertIn("network waking", self.stream.auto_error)
            self.stream._flush_mode()
            self.assertEqual(request.call_count, 1)
            now.return_value = 12.0
            self.stream._flush_mode()
            self.assertEqual(self.stream.auto_error, "")
            self.assertIsNone(self.stream._mode_pending)
            request.assert_called_with("http://192.0.2.1/api/mode/auto", timeout=4)

    def test_new_mode_supersedes_failed_or_inflight_request(self):
        def fail(*args, **kwargs):
            self.stream._send_mode("auto")
            raise OSError("old request failed")

        self.stream._send_mode("viz")
        with patch.object(audio, "urlopen", side_effect=fail):
            self.stream._flush_mode()
        self.assertEqual(self.stream._mode_pending, "auto")
        self.assertEqual(self.stream.auto_error, "")
        self.assertEqual(self.stream._mode_retry_at, 0.0)

    def test_target_change_cancels_pending_old_mode(self):
        self.stream._send_mode("auto")
        self.stream.auto.forced = True
        self.stream.set_target("new-clock.local", 4210)
        self.assertEqual(self.stream._mode_pending, "viz")
        self.assertIsNone(self.stream._target)

    def test_reopened_device_does_not_loop_on_stale_default_id(self):
        self.stream._default_device_id = "old-speaker"
        recorder = Mock()
        recorder.__enter__ = Mock(return_value=recorder)
        recorder.__exit__ = Mock(return_value=False)
        calls = []

        def record(**kwargs):
            calls.append(1)
            if len(calls) == 2:
                self.stream.stop()
            return audio.np.zeros((audio.FRAMES, 2), dtype=audio.np.float32)

        recorder.record.side_effect = record
        mic = Mock()
        mic.recorder.return_value = recorder
        with patch.object(audio.sc, "default_speaker", return_value=Mock(id="new-speaker")) as speaker, \
                patch.object(audio.sc, "get_microphone", return_value=mic):
            self.stream._capture_loop()
        speaker.assert_called_once()
        self.assertEqual(len(calls), 2)

    def check_device(self, now, uptime, forced=False):
        reply = Mock()
        reply.read.return_value = audio.json.dumps({"uptime": uptime, "forcedViz": forced}).encode()
        reply.__enter__ = Mock(return_value=reply)
        reply.__exit__ = Mock(return_value=False)
        with patch.object(audio, "urlopen", return_value=reply), \
                patch.object(audio.time, "monotonic", return_value=now):
            self.stream._check_device_restart()

    def test_device_restart_reasserts_active_playback(self):
        self.stream._target = ("192.0.2.1", 4210)
        self.stream.auto.enabled = self.stream.auto.forced = True
        self.check_device(100, 50, True)
        self.check_device(200, 3)
        self.assertEqual(self.stream._mode_pending, "viz")
        self.assertFalse(self.stream._device_viz)

    def test_manual_stop_is_not_treated_as_a_device_restart(self):
        self.stream._target = ("192.0.2.1", 4210)
        self.stream.auto.enabled = self.stream.auto.forced = True
        self.check_device(100, 50, True)
        self.check_device(110, 60)
        self.assertIsNone(self.stream._mode_pending)
        self.assertFalse(self.stream._device_viz)

    def test_restart_during_silence_does_not_force_visualizer(self):
        self.stream._target = ("192.0.2.1", 4210)
        self.stream.auto.enabled = True
        self.check_device(100, 50)
        self.check_device(200, 3)
        self.assertIsNone(self.stream._mode_pending)


if __name__ == "__main__":
    unittest.main()
