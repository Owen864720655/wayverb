#include "CommandIDs.h"
#include "ImpulseViewer.hpp"
#include "Main.hpp"

ImpulseViewer::ImpulseViewer(AudioDeviceManager& audio_device_manager,
                             AudioFormatManager& audio_format_manager,
                             const File& file)
        : audio_device_manager(audio_device_manager)
        , audio_format_reader_source(audio_format_manager.createReaderFor(file),
                                     true)
        , renderer(audio_transport_source, audio_format_manager, file)
        , ruler(playback_view_manager)
        , waterfall_button("waterfall")
        , waveform_button("waveform")
        , follow_playback_button("follow playback")
        , scroll_bar(false)
        , transport(audio_transport_source) {
    audio_transport_source.setSource(
            &audio_format_reader_source,
            0,
            nullptr,
            audio_format_reader_source.getAudioFormatReader()->sampleRate);
    audio_source_player.setSource(&audio_transport_source);
    audio_device_manager.addAudioCallback(&audio_source_player);

    auto& command_manager = VisualiserApplication::get_command_manager();
    command_manager.registerAllCommandsForTarget(this);

    auto r = Range<double>(0, audio_transport_source.getLengthInSeconds());
    playback_view_manager.set_max_range(r, true);
    playback_view_manager.set_visible_range(r, true);

    scroll_bar.setAutoHide(false);

    for (auto i : {&waterfall_button, &waveform_button}) {
        i->setRadioGroupId(0xf);
        i->setClickingTogglesState(true);
        i->setWantsKeyboardFocus(false);
    }

    waveform_button.setToggleState(true, sendNotification);

    follow_playback_button.setWantsKeyboardFocus(false);
    follow_playback_button.setToggleState(true, true);

    addAndMakeVisible(renderer);
    addAndMakeVisible(waterfall_button);
    addAndMakeVisible(waveform_button);
    addAndMakeVisible(follow_playback_button);
    addAndMakeVisible(scroll_bar);
    addAndMakeVisible(transport);
    addAndMakeVisible(ruler);

    setWantsKeyboardFocus(true);
}

ImpulseViewer::~ImpulseViewer() noexcept {
    audio_device_manager.removeAudioCallback(&audio_source_player);
}

void ImpulseViewer::max_range_changed(PlaybackViewManager* r,
                                      const Range<double>& range) {
    scroll_bar.setRangeLimits(range);
}
void ImpulseViewer::visible_range_changed(PlaybackViewManager* r,
                                          const Range<double>& range) {
    scroll_bar.setCurrentRange(range, dontSendNotification);
    renderer.get_renderer().set_visible_range(range);
}
void ImpulseViewer::current_time_changed(PlaybackViewManager* r, double time) {
}

void ImpulseViewer::scrollBarMoved(ScrollBar* s, double new_range_start) {
    if (s == &scroll_bar) {
        playback_view_manager.set_visible_range(scroll_bar.getCurrentRange(),
                                                true);
    }
}

void ImpulseViewer::timerCallback() {
    playback_view_manager.set_current_time(
            audio_transport_source.getCurrentPosition(), true);
}

void ImpulseViewer::resized() {
    auto bounds = getLocalBounds();

    scroll_bar.setBounds(bounds.removeFromBottom(20));

    auto top = bounds.removeFromTop(40);
    top.reduce(2, 2);
    ruler.setBounds(bounds.removeFromTop(20));
    renderer.setBounds(bounds);

    transport.setBounds(top.removeFromLeft(200));

    follow_playback_button.setBounds(top.removeFromRight(150));

    waveform_button.setBounds(top.removeFromLeft(100));
    top.removeFromLeft(2);
    waterfall_button.setBounds(top.removeFromLeft(100));
}

void ImpulseViewer::getAllCommands(Array<CommandID>& commands) {
    commands.addArray({CommandIDs::idPlay,
                       CommandIDs::idPause,
                       CommandIDs::idReturnToBeginning});
}
void ImpulseViewer::getCommandInfo(CommandID command_id,
                                   ApplicationCommandInfo& result) {
    switch (command_id) {
        case CommandIDs::idPlay:
            result.setInfo("Play", "Start playback", "General", 0);
            result.defaultKeypresses.add(
                    KeyPress(KeyPress::spaceKey, ModifierKeys::noModifiers, 0));
            result.setActive(!audio_transport_source.isPlaying());
            break;

        case CommandIDs::idPause:
            result.setInfo("Pause", "Pause playback", "General", 0);
            result.defaultKeypresses.add(
                    KeyPress(KeyPress::spaceKey, ModifierKeys::noModifiers, 0));
            result.setActive(audio_transport_source.isPlaying());
            break;

        case CommandIDs::idReturnToBeginning:
            result.setInfo("Go to Start",
                           "Move playhead to the beginning",
                           "General",
                           0);
            result.defaultKeypresses.add(KeyPress(
                    KeyPress::returnKey, ModifierKeys::noModifiers, 0));
            break;
        default:
            break;
    }
}
bool ImpulseViewer::perform(const InvocationInfo& info) {
    switch (info.commandID) {
        case CommandIDs::idPlay:
            audio_transport_source.start();
            VisualiserApplication::get_command_manager().commandStatusChanged();
            startTimer(25);
            return true;

        case CommandIDs::idPause:
            audio_transport_source.stop();
            VisualiserApplication::get_command_manager().commandStatusChanged();
            stopTimer();
            timerCallback();
            return true;

        case CommandIDs::idReturnToBeginning:
            audio_transport_source.setPosition(0);
            timerCallback();
            return true;

        default:
            return false;
    }
}
ApplicationCommandTarget* ImpulseViewer::getNextCommandTarget() {
    return findFirstTargetParentComponent();
}

void ImpulseViewer::buttonClicked(Button* b) {
    if (b == &waterfall_button) {
        renderer.get_renderer().set_mode(ImpulseRenderer::Mode::waterfall);
    } else if (b == &waveform_button) {
        renderer.get_renderer().set_mode(ImpulseRenderer::Mode::waveform);
    } else if (b == &follow_playback_button) {
        playback_view_manager.set_follow_playback(
                follow_playback_button.getToggleState());
    }
}