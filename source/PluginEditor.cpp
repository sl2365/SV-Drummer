#include "PluginEditor.h"

#include <algorithm>
#include <functional>
#include <vector>

namespace
{
const juce::Colour backgroundColour (0xff111316);
const juce::Colour panelColour (0xff191c20);
const juce::Colour raisedPanelColour (0xff20242a);
const juce::Colour lineColour (0xff343940);
const juce::Colour textColour (0xffe8ebef);
const juce::Colour mutedTextColour (0xff8d949e);

juce::Colour getPadColour (int index)
{
    static const std::array<juce::Colour, 16> colours
    {
        juce::Colour (0xffdf5b57), juce::Colour (0xffd77c75),
        juce::Colour (0xffdf874b), juce::Colour (0xffc5a24f),
        juce::Colour (0xffcf6389), juce::Colour (0xffad5b8c),
        juce::Colour (0xff8348a7), juce::Colour (0xff7163b6),
        juce::Colour (0xff526fc2), juce::Colour (0xff438dcc),
        juce::Colour (0xff3ca7b6), juce::Colour (0xff45a58b),
        juce::Colour (0xff59a965), juce::Colour (0xff84ad50),
        juce::Colour (0xffb5a943), juce::Colour (0xffcc7850)
    };

    return colours[static_cast<std::size_t> (juce::jlimit (0, 15, index))];
}

juce::String midiNoteDescription (int note)
{
    return juce::String (note) + "  "
         + juce::MidiMessage::getMidiNoteName (note, true, true, 3);
}

bool isSupportedPath (const juce::String& path)
{
    return SVDrummerAudioProcessor::isSupportedAudioFile (juce::File (path));
}
}

class SVDrummerLookAndFeel final : public juce::LookAndFeel_V4
{
public:
    SVDrummerLookAndFeel()
    {
        setColour (juce::Label::textColourId, textColour);
        setColour (juce::Label::textWhenEditingColourId, textColour);
        setColour (juce::Slider::textBoxTextColourId, textColour);
        setColour (juce::Slider::textBoxBackgroundColourId, panelColour);
        setColour (juce::Slider::textBoxOutlineColourId, lineColour);
        setColour (juce::PopupMenu::backgroundColourId, raisedPanelColour);
        setColour (juce::PopupMenu::textColourId, textColour);
        setColour (juce::PopupMenu::highlightedBackgroundColourId, juce::Colour (0xff405064));
    }

    void drawButtonBackground (juce::Graphics& g,
                               juce::Button& button,
                               const juce::Colour& background,
                               bool highlighted,
                               bool down) override
    {
        auto bounds = button.getLocalBounds().toFloat().reduced (0.5f);
        auto colour = background;

        if (down)
            colour = colour.brighter (0.18f);
        else if (highlighted)
            colour = colour.brighter (0.09f);

        g.setColour (colour);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (lineColour.brighter (highlighted ? 0.22f : 0.0f));
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);
    }

    void drawRotarySlider (juce::Graphics& g,
                           int x, int y, int width, int height,
                           float position,
                           float startAngle,
                           float endAngle,
                           juce::Slider& slider) override
    {
        const auto radius = static_cast<float> (juce::jmin (width, height)) * 0.40f;
        const auto centre = juce::Point<float> (static_cast<float> (x + width) * 0.5f,
                                                static_cast<float> (y + height) * 0.5f);
        const auto angle = startAngle + position * (endAngle - startAngle);
        const auto accent = slider.findColour (juce::Slider::rotarySliderFillColourId);

        juce::Path track;
        track.addCentredArc (centre.x, centre.y, radius, radius,
                             0.0f, startAngle, endAngle, true);
        g.setColour (lineColour);
        g.strokePath (track, juce::PathStrokeType (4.0f,
                                                   juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));

        juce::Path fill;
        fill.addCentredArc (centre.x, centre.y, radius, radius,
                            0.0f, startAngle, angle, true);
        g.setColour (accent);
        g.strokePath (fill, juce::PathStrokeType (4.0f,
                                                  juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::rounded));

        g.setColour (juce::Colour (0xff2b3036));
        g.fillEllipse (centre.x - radius * 0.72f,
                       centre.y - radius * 0.72f,
                       radius * 1.44f,
                       radius * 1.44f);
        g.setColour (accent);
        g.drawLine (centre.x, centre.y,
                    centre.x + std::sin (angle) * radius * 0.58f,
                    centre.y - std::cos (angle) * radius * 0.58f,
                    2.0f);
    }
};

class SampleTreeRow;

class SampleTreeItem final : public juce::TreeViewItem
{
public:
    SampleTreeItem (juce::File itemFile, juce::File libraryRoot, bool topLevel)
        : file (std::move (itemFile)),
          rootFolder (std::move (libraryRoot)),
          isTopLevel (topLevel)
    {
    }

    bool mightContainSubItems() override
    {
        return file.isDirectory();
    }

    juce::String getUniqueName() const override
    {
        return file.getFullPathName();
    }

    void itemOpennessChanged (bool isNowOpen) override
    {
        if (! isNowOpen || ! file.isDirectory())
            return;

        clearSubItems();

        juce::Array<juce::File> directories;
        juce::Array<juce::File> samples;
        for (const auto entry : juce::RangedDirectoryIterator (
                 file, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto child = entry.getFile();

            if (entry.isHidden())
                continue;

            if (child.isDirectory())
                directories.add (child);
            else if (SVDrummerAudioProcessor::isSupportedAudioFile (child))
                samples.add (child);
        }

        struct FileSorter
        {
            static int compareElements (const juce::File& first, const juce::File& second)
            {
                return first.getFileName().compareNatural (second.getFileName());
            }
        } sorter;

        directories.sort (sorter);
        samples.sort (sorter);

        for (const auto& directory : directories)
            addSubItem (new SampleTreeItem (directory, rootFolder, false));

        for (const auto& sample : samples)
            addSubItem (new SampleTreeItem (sample, rootFolder, false));
    }

    void paintItem (juce::Graphics& g, int width, int height) override
    {
        paintRow (g, width, height);
    }

    std::unique_ptr<juce::Component> createItemComponent() override;

    void paintRow (juce::Graphics& g, int width, int height)
    {
        const bool directory = file.isDirectory();
        const auto colour = directory ? (isTopLevel ? juce::Colour (0xffd9dde4)
                                                     : juce::Colour (0xffb6bdc8))
                                      : juce::Colour (0xffa4abb4);

        if (isSelected())
        {
            g.setColour (juce::Colour (0xff354252));
            g.fillRoundedRectangle (0.0f, 1.0f,
                                    static_cast<float> (width),
                                    static_cast<float> (height - 2), 2.0f);
        }

        const auto iconBounds = juce::Rectangle<float> (3.0f, 4.0f, 12.0f, 12.0f);
        g.setColour (directory ? juce::Colour (0xffd09c50) : juce::Colour (0xff6c7887));

        if (directory)
        {
            g.fillRoundedRectangle (iconBounds, 1.5f);
            g.fillRect (juce::Rectangle<float> (5.0f, 2.0f, 6.0f, 4.0f));
        }
        else
        {
            const float middle = iconBounds.getCentreY();
            juce::Path wave;
            wave.startNewSubPath (iconBounds.getX(), middle);

            for (int point = 0; point < 7; ++point)
            {
                const float px = iconBounds.getX() + static_cast<float> (point) * 2.0f;
                const float py = middle + ((point % 2 == 0) ? -3.0f : 3.0f);
                wave.lineTo (px, py);
            }

            g.strokePath (wave, juce::PathStrokeType (1.4f));
        }

        g.setColour (colour);
        g.setFont (isTopLevel ? 13.0f : 12.5f);
        g.drawText (file.getFileName(), 21, 0, width - 23, height,
                    juce::Justification::centredLeft, true);
    }

    const juce::File& getFile() const noexcept       { return file; }
    const juce::File& getRootFolder() const noexcept { return rootFolder; }

private:
    juce::File file;
    juce::File rootFolder;
    bool isTopLevel = false;
};

class SampleTreeRow final : public juce::Component
{
public:
    explicit SampleTreeRow (SampleTreeItem& owner) : item (owner) {}

    void paint (juce::Graphics& g) override
    {
        item.paintRow (g, getWidth(), getHeight());
    }

    void mouseDown (const juce::MouseEvent&) override
    {
        dragStarted = false;
        item.setSelected (true, true);
    }

    void mouseDoubleClick (const juce::MouseEvent&) override
    {
        if (item.getFile().isDirectory())
            item.setOpen (! item.isOpen());
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (dragStarted || event.getDistanceFromDragStart() < 5
            || ! item.getFile().existsAsFile()
            || ! SVDrummerAudioProcessor::isSupportedAudioFile (item.getFile()))
            return;

        if (auto* container = findParentComponentOfClass<juce::DragAndDropContainer>())
        {
            dragStarted = true;
            container->startDragging (item.getFile().getFullPathName(), this);
        }
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        dragStarted = false;
    }

private:
    SampleTreeItem& item;
    bool dragStarted = false;
};

std::unique_ptr<juce::Component> SampleTreeItem::createItemComponent()
{
    return std::make_unique<SampleTreeRow> (*this);
}

class SampleTreeRoot final : public juce::TreeViewItem
{
public:
    bool mightContainSubItems() override { return true; }
    juce::String getUniqueName() const override { return "SVDRUMMER_BROWSER_ROOT"; }
    void paintItem (juce::Graphics&, int, int) override {}
};

class SampleBrowserTree final : public juce::TreeView
{
public:
    SampleBrowserTree()
    {
        setRootItem (&rootItem);
        setRootItemVisible (false);
        setDefaultOpenness (false);
        setIndentSize (15);
        setColour (juce::TreeView::backgroundColourId, panelColour);
        setColour (juce::TreeView::linesColourId, lineColour);
    }

    ~SampleBrowserTree() override
    {
        setRootItem (nullptr);
    }

    void rebuild (const juce::StringArray& folders)
    {
        rootItem.clearSubItems();

        for (const auto& path : folders)
        {
            const juce::File folder (path);

            if (folder.isDirectory())
                rootItem.addSubItem (new SampleTreeItem (folder, folder, true));
        }

        rootItem.setOpen (true);

        for (int index = 0; index < rootItem.getNumSubItems(); ++index)
            if (auto* child = rootItem.getSubItem (index))
                child->setOpen (true);

        repaint();
    }

    juce::File getSelectedRootFolder() const
    {
        if (auto* item = dynamic_cast<SampleTreeItem*> (getSelectedItem (0)))
            return item->getRootFolder();

        return {};
    }

private:
    SampleTreeRoot rootItem;
};

class SVDrummerBrowserPanel final : public juce::Component
{
public:
    explicit SVDrummerBrowserPanel (SVDrummerAudioProcessor& owner)
        : processor (owner)
    {
        samplesButton.setClickingTogglesState (false);
        patternsButton.setClickingTogglesState (false);

        samplesButton.onClick = [this] { showSamples (true); };
        patternsButton.onClick = [this] { showSamples (false); };
        addFolderButton.onClick = [this] { chooseFolder(); };
        removeFolderButton.onClick = [this]
        {
            const auto root = tree.getSelectedRootFolder();

            if (root.isDirectory())
            {
                processor.removeBrowserFolder (root);
                refresh();
            }
        };
        refreshButton.onClick = [this] { refresh(); };

        addAndMakeVisible (samplesButton);
        addAndMakeVisible (patternsButton);
        addAndMakeVisible (addFolderButton);
        addAndMakeVisible (removeFolderButton);
        addAndMakeVisible (refreshButton);
        addAndMakeVisible (tree);
        addAndMakeVisible (emptyLabel);

        emptyLabel.setText ("Add one or more sample folders.\n\n"
                            "Drag supported samples from this tree onto a pad.",
                            juce::dontSendNotification);
        emptyLabel.setJustificationType (juce::Justification::centred);
        emptyLabel.setColour (juce::Label::textColourId, mutedTextColour);

        showSamples (true);
        refresh();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panelColour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (lineColour);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);

        auto title = getLocalBounds().reduced (10).removeFromTop (24);
        g.setColour (textColour);
        g.setFont (14.0f);
        g.drawText (showingSamples ? "SAMPLE LIBRARIES" : "PATTERN LIBRARY",
                    title, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (8);
        area.removeFromTop (28);

        auto modeRow = area.removeFromTop (27);
        samplesButton.setBounds (modeRow.removeFromLeft (modeRow.getWidth() / 2).reduced (1));
        patternsButton.setBounds (modeRow.reduced (1));

        area.removeFromTop (6);
        auto tools = area.removeFromTop (26);
        addFolderButton.setBounds (tools.removeFromLeft (tools.getWidth() * 44 / 100).reduced (1));
        removeFolderButton.setBounds (tools.removeFromLeft (tools.getWidth() * 30 / 56).reduced (1));
        refreshButton.setBounds (tools.reduced (1));
        area.removeFromTop (6);

        tree.setBounds (area);
        emptyLabel.setBounds (area.reduced (16));
    }

    void refresh()
    {
        const auto folders = processor.getBrowserFolders();
        tree.rebuild (folders);
        emptyLabel.setVisible (showingSamples && folders.isEmpty());
    }

private:
    void showSamples (bool shouldShowSamples)
    {
        showingSamples = shouldShowSamples;
        samplesButton.setColour (juce::TextButton::buttonColourId,
                                 showingSamples ? juce::Colour (0xff3e536a)
                                                : raisedPanelColour);
        patternsButton.setColour (juce::TextButton::buttonColourId,
                                  showingSamples ? raisedPanelColour
                                                 : juce::Colour (0xff3e536a));

        tree.setVisible (showingSamples);
        addFolderButton.setVisible (showingSamples);
        removeFolderButton.setVisible (showingSamples);
        refreshButton.setVisible (showingSamples);

        if (! showingSamples)
        {
            emptyLabel.setText ("Pattern browsing and drag assignment\n"
                                "will be activated with the sequencer engine.",
                                juce::dontSendNotification);
            emptyLabel.setVisible (true);
        }
        else
        {
            emptyLabel.setText ("Add one or more sample folders.\n\n"
                                "Drag supported samples from this tree onto a pad.",
                                juce::dontSendNotification);
            emptyLabel.setVisible (processor.getBrowserFolders().isEmpty());
        }

        repaint();
    }

    void chooseFolder()
    {
        auto initialFolder = processor.getPortableSamplesDirectory();
        initialFolder.createDirectory();

        folderChooser = std::make_unique<juce::FileChooser> (
            "Add a sample-library folder", initialFolder, juce::String(), true);

        folderChooser->launchAsync (
            juce::FileBrowserComponent::openMode
                | juce::FileBrowserComponent::canSelectDirectories,
            [this] (const juce::FileChooser& chooser)
            {
                const auto result = chooser.getResult();

                if (result.isDirectory())
                {
                    processor.addBrowserFolder (result);
                    refresh();
                }
            });
    }

    SVDrummerAudioProcessor& processor;
    juce::TextButton samplesButton { "SAMPLES" };
    juce::TextButton patternsButton { "PATTERNS" };
    juce::TextButton addFolderButton { "+ FOLDER" };
    juce::TextButton removeFolderButton { "REMOVE" };
    juce::TextButton refreshButton { "REFRESH" };
    SampleBrowserTree tree;
    juce::Label emptyLabel;
    std::unique_ptr<juce::FileChooser> folderChooser;
    bool showingSamples = true;
};

class SVDrummerPadComponent final : public juce::Component,
                                    public juce::FileDragAndDropTarget,
                                    public juce::DragAndDropTarget
{
public:
    SVDrummerPadComponent (SVDrummerAudioProcessor& owner,
                           int padNumber,
                           std::function<void (int)> settingsCallback)
        : processor (owner),
          padIndex (padNumber),
          accent (getPadColour (padNumber)),
          onSettingsRequested (std::move (settingsCallback))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        lastActivityCounter = processor.getPadActivityCounter (padIndex);
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        const bool muted = processor.isPadMuted (padIndex);
        const bool soloed = processor.isPadSoloed (padIndex);
        const auto effectiveAccent = muted ? accent.withSaturation (0.18f).darker (0.2f)
                                           : accent;

        g.setColour (juce::Colour (0xff15181c));
        g.fillRoundedRectangle (bounds, 4.0f);

        if (flashAmount > 0.0f)
        {
            g.setColour (effectiveAccent.withAlpha (0.15f + flashAmount * 0.32f));
            g.fillRoundedRectangle (bounds.reduced (1.0f), 4.0f);
        }

        if (dropHighlight)
        {
            g.setColour (juce::Colours::white.withAlpha (0.12f));
            g.fillRoundedRectangle (bounds.reduced (2.0f), 3.0f);
        }

        g.setColour (effectiveAccent.withAlpha (soloed ? 1.0f : 0.82f));
        g.drawRoundedRectangle (bounds.reduced (0.75f), 4.0f, soloed ? 2.0f : 1.2f);

        const auto footer = getFooterBounds().toFloat();
        g.setColour (effectiveAccent.withAlpha (muted ? 0.22f : 0.82f));
        g.fillRect (footer);

        const auto sampleName = processor.getPadDisplayName (padIndex);
        g.setColour (muted ? mutedTextColour : textColour);
        g.setFont (juce::jmax (10.0f, static_cast<float> (getHeight()) * 0.095f));
        g.drawText (sampleName,
                    getLocalBounds().reduced (7).removeFromTop (20),
                    juce::Justification::centredLeft, true);

        g.setColour (mutedTextColour);
        g.setFont (9.5f);
        g.drawText (juce::String (padIndex + 1).paddedLeft ('0', 2),
                    getLocalBounds().reduced (6).removeFromTop (18),
                    juce::Justification::centredRight, false);

        drawWaveform (g, effectiveAccent);
        drawFooter (g, footer, muted, soloed);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        const auto point = event.getPosition();

        if (noteBounds.contains (point))
        {
            showNoteMenu();
            return;
        }

        if (muteBounds.contains (point))
        {
            processor.setPadMuted (padIndex, ! processor.isPadMuted (padIndex));
            repaint();
            return;
        }

        if (soloBounds.contains (point))
        {
            processor.setPadSoloed (padIndex, ! processor.isPadSoloed (padIndex));
            repaint();
            return;
        }

        if (settingsBounds.contains (point))
        {
            if (onSettingsRequested)
                onSettingsRequested (padIndex);
            return;
        }

        processor.triggerPadFromInterface (padIndex, 1.0f);
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (! noteBounds.contains (event.getPosition()) || wheel.deltaY == 0.0f)
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        processor.setPadMidiNote (
            padIndex,
            processor.getPadMidiNote (padIndex) + (wheel.deltaY > 0.0f ? 1 : -1));
        repaint();
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() > 0 && isSupportedPath (files[0]);
    }

    void fileDragEnter (const juce::StringArray&, int, int) override
    {
        dropHighlight = true;
        repaint();
    }

    void fileDragExit (const juce::StringArray&) override
    {
        dropHighlight = false;
        repaint();
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        dropHighlight = false;

        if (files.size() > 0)
            loadFile (juce::File (files[0]));
    }

    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return isSupportedPath (details.description.toString());
    }

    void itemDragEnter (const SourceDetails&) override
    {
        dropHighlight = true;
        repaint();
    }

    void itemDragExit (const SourceDetails&) override
    {
        dropHighlight = false;
        repaint();
    }

    void itemDropped (const SourceDetails& details) override
    {
        dropHighlight = false;
        loadFile (juce::File (details.description.toString()));
    }

    void refreshAnimation()
    {
        const auto currentCounter = processor.getPadActivityCounter (padIndex);

        if (currentCounter != lastActivityCounter)
        {
            lastActivityCounter = currentCounter;
            flashAmount = 1.0f;
        }
        else
        {
            flashAmount = juce::jmax (0.0f, flashAmount - 0.12f);
        }

        repaint();
    }

private:
    juce::Rectangle<int> getFooterBounds()
    {
        return getLocalBounds().removeFromBottom (juce::jmax (22, getHeight() / 5));
    }

    void drawWaveform (juce::Graphics& g, juce::Colour colour)
    {
        const auto sample = processor.getPadSample (padIndex);
        auto waveformBounds = getLocalBounds().reduced (7);
        waveformBounds.removeFromTop (20);
        waveformBounds.removeFromBottom (getFooterBounds().getHeight() - 4);

        if (sample == nullptr || sample->waveform.empty())
        {
            g.setColour (mutedTextColour.withAlpha (0.55f));
            g.setFont (10.0f);
            g.drawText ("DROP SAMPLE", waveformBounds,
                        juce::Justification::centred, false);
            return;
        }

        juce::Path waveform;
        const float middle = static_cast<float> (waveformBounds.getCentreY());
        const float halfHeight = static_cast<float> (waveformBounds.getHeight()) * 0.45f;
        const float left = static_cast<float> (waveformBounds.getX());
        const float width = static_cast<float> (waveformBounds.getWidth());
        const int points = static_cast<int> (sample->waveform.size());

        for (int point = 0; point < points; ++point)
        {
            const float x = left + width * static_cast<float> (point)
                                   / static_cast<float> (juce::jmax (1, points - 1));
            const auto range = sample->waveform[static_cast<std::size_t> (point)];
            waveform.startNewSubPath (x, middle - range.getEnd() * halfHeight);
            waveform.lineTo (x, middle - range.getStart() * halfHeight);
        }

        g.setColour (colour.withAlpha (0.94f));
        g.strokePath (waveform, juce::PathStrokeType (1.2f));
        g.setColour (colour.withAlpha (0.18f));
        g.drawHorizontalLine (static_cast<int> (middle),
                              static_cast<float> (waveformBounds.getX()),
                              static_cast<float> (waveformBounds.getRight()));
    }

    void drawFooter (juce::Graphics& g,
                     juce::Rectangle<float> footer,
                     bool muted,
                     bool soloed)
    {
        auto footerInt = footer.toNearestInt();
        noteBounds = footerInt.removeFromLeft (footerInt.getWidth() * 52 / 100);
        muteBounds = footerInt.removeFromLeft (footerInt.getWidth() * 31 / 100);
        soloBounds = footerInt.removeFromLeft (footerInt.getWidth() / 2);
        settingsBounds = footerInt;

        g.setColour (juce::Colours::black.withAlpha (0.30f));
        g.drawVerticalLine (noteBounds.getRight(), footer.getY(), footer.getBottom());
        g.drawVerticalLine (muteBounds.getRight(), footer.getY(), footer.getBottom());
        g.drawVerticalLine (soloBounds.getRight(), footer.getY(), footer.getBottom());

        g.setFont (juce::jmax (9.0f, footer.getHeight() * 0.40f));
        g.setColour (juce::Colours::white.withAlpha (0.94f));
        g.drawText (midiNoteDescription (processor.getPadMidiNote (padIndex)),
                    noteBounds.reduced (3, 0), juce::Justification::centred, true);

        g.setColour (muted ? juce::Colours::white : juce::Colours::white.withAlpha (0.62f));
        g.drawText ("M", muteBounds, juce::Justification::centred, false);
        g.setColour (soloed ? juce::Colours::white : juce::Colours::white.withAlpha (0.62f));
        g.drawText ("S", soloBounds, juce::Justification::centred, false);

        drawCog (g, settingsBounds.toFloat());
    }

    void drawCog (juce::Graphics& g, juce::Rectangle<float> bounds)
    {
        const auto centre = bounds.getCentre();
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.22f;
        g.setColour (juce::Colours::white.withAlpha (0.72f));
        g.drawEllipse (centre.x - radius, centre.y - radius,
                       radius * 2.0f, radius * 2.0f, 1.3f);
        g.fillEllipse (centre.x - 1.5f, centre.y - 1.5f, 3.0f, 3.0f);

        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const float angle = juce::MathConstants<float>::twoPi
                              * static_cast<float> (tooth) / 8.0f;
            g.drawLine (centre.x + std::sin (angle) * radius,
                        centre.y - std::cos (angle) * radius,
                        centre.x + std::sin (angle) * radius * 1.38f,
                        centre.y - std::cos (angle) * radius * 1.38f,
                        1.2f);
        }
    }

    void showNoteMenu()
    {
        juce::PopupMenu menu;
        const int currentNote = processor.getPadMidiNote (padIndex);

        for (int group = 0; group < 11; ++group)
        {
            juce::PopupMenu octaveMenu;
            const int firstNote = group * 12;
            const int lastNote = juce::jmin (127, firstNote + 11);

            for (int note = firstNote; note <= lastNote; ++note)
                octaveMenu.addItem (note + 1, midiNoteDescription (note),
                                    true, note == currentNote);

            menu.addSubMenu ("Notes " + juce::String (firstNote)
                                 + "-" + juce::String (lastNote),
                             octaveMenu);
        }

        juce::Component::SafePointer<SVDrummerPadComponent> safeThis (this);
        menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this),
                            [safeThis] (int result)
                            {
                                if (safeThis != nullptr && result > 0)
                                {
                                    safeThis->processor.setPadMidiNote (
                                        safeThis->padIndex, result - 1);
                                    safeThis->repaint();
                                }
                            });
    }

    void loadFile (const juce::File& file)
    {
        const auto result = processor.loadSampleIntoPad (padIndex, file);

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "Could not load the sample:\n\n" + result.getErrorMessage());
        }

        repaint();
    }

    SVDrummerAudioProcessor& processor;
    int padIndex = 0;
    juce::Colour accent;
    std::function<void (int)> onSettingsRequested;
    juce::Rectangle<int> noteBounds;
    juce::Rectangle<int> muteBounds;
    juce::Rectangle<int> soloBounds;
    juce::Rectangle<int> settingsBounds;
    std::uint64_t lastActivityCounter = 0;
    float flashAmount = 0.0f;
    bool dropHighlight = false;
};

class SVDrummerPadSettingsPanel final : public juce::Component
{
public:
    explicit SVDrummerPadSettingsPanel (SVDrummerAudioProcessor& owner)
        : processor (owner)
    {
        configureSlider (volumeSlider, volumeLabel, "VOLUME", -60.0, 6.0, 0.1);
        volumeSlider.setTextValueSuffix (" dB");
        configureSlider (panSlider, panLabel, "PAN", -1.0, 1.0, 0.01);
        configureSlider (tuneSlider, tuneLabel, "TUNE", -24.0, 24.0, 0.01);
        tuneSlider.setTextValueSuffix (" st");

        panSlider.textFromValueFunction = [] (double value)
        {
            if (std::abs (value) < 0.005)
                return juce::String ("C");

            return value < 0.0 ? "L " + juce::String (std::abs (value), 2)
                               : "R " + juce::String (value, 2);
        };

        volumeSlider.onValueChange = [this]
        {
            if (! updating)
                processor.setPadVolumeDb (padIndex, static_cast<float> (volumeSlider.getValue()));
        };
        panSlider.onValueChange = [this]
        {
            if (! updating)
                processor.setPadPan (padIndex, static_cast<float> (panSlider.getValue()));
        };
        tuneSlider.onValueChange = [this]
        {
            if (! updating)
                processor.setPadTuneSemitones (padIndex, static_cast<float> (tuneSlider.getValue()));
        };

        reverseButton.setClickingTogglesState (true);
        reverseButton.onClick = [this]
        {
            if (! updating)
                processor.setPadReversed (padIndex, reverseButton.getToggleState());
        };
        clearButton.onClick = [this]
        {
            processor.clearPadSample (padIndex);
            syncFromProcessor();
        };

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (pathLabel);
        addAndMakeVisible (reverseButton);
        addAndMakeVisible (clearButton);

        titleLabel.setColour (juce::Label::textColourId, textColour);
        titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        pathLabel.setColour (juce::Label::textColourId, mutedTextColour);
        pathLabel.setJustificationType (juce::Justification::centredLeft);
        pathLabel.setMinimumHorizontalScale (0.6f);

        setPadIndex (0);
    }

    void setPadIndex (int newPadIndex)
    {
        padIndex = juce::jlimit (0, SVDrummerAudioProcessor::numberOfPads - 1,
                                newPadIndex);
        syncFromProcessor();
        repaint();
    }

    void syncFromProcessor()
    {
        const juce::ScopedValueSetter<bool> setter (updating, true);
        volumeSlider.setValue (processor.getPadVolumeDb (padIndex), juce::dontSendNotification);
        panSlider.setValue (processor.getPadPan (padIndex), juce::dontSendNotification);
        tuneSlider.setValue (processor.getPadTuneSemitones (padIndex), juce::dontSendNotification);
        reverseButton.setToggleState (processor.isPadReversed (padIndex),
                                      juce::dontSendNotification);

        titleLabel.setText ("PAD " + juce::String (padIndex + 1).paddedLeft ('0', 2)
                                + "  —  " + processor.getPadDisplayName (padIndex),
                            juce::dontSendNotification);

        auto path = processor.getPadSamplePath (padIndex);
        pathLabel.setText (path.isNotEmpty() ? path : "No sample loaded",
                           juce::dontSendNotification);
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panelColour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (lineColour);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);

        auto strip = getLocalBounds().toFloat().removeFromLeft (5.0f).reduced (1.0f);
        g.setColour (getPadColour (padIndex));
        g.fillRoundedRectangle (strip, 2.0f);

        auto footer = getLocalBounds().reduced (14).removeFromBottom (42);
        g.setColour (mutedTextColour);
        g.setFont (12.0f);
        g.drawText ("Advanced waveform editing, envelopes, filter, LFO, looping and choke groups "
                    "will be added in later stages.",
                    footer, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        auto titleRow = area.removeFromTop (26);
        titleLabel.setBounds (titleRow);
        area.removeFromTop (2);
        pathLabel.setBounds (area.removeFromTop (24));
        area.removeFromTop (10);

        auto controls = area.removeFromTop (juce::jmin (150, area.getHeight() - 48));
        const int knobWidth = juce::jmax (95, controls.getWidth() / 5);
        layoutKnob (volumeLabel, volumeSlider, controls.removeFromLeft (knobWidth));
        layoutKnob (panLabel, panSlider, controls.removeFromLeft (knobWidth));
        layoutKnob (tuneLabel, tuneSlider, controls.removeFromLeft (knobWidth));

        auto buttons = controls.reduced (6);
        reverseButton.setBounds (buttons.removeFromTop (32));
        buttons.removeFromTop (8);
        clearButton.setBounds (buttons.removeFromTop (32));
    }

private:
    void configureSlider (juce::Slider& slider,
                          juce::Label& label,
                          const juce::String& labelText,
                          double minimum,
                          double maximum,
                          double interval)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 76, 18);
        slider.setRange (minimum, maximum, interval);
        slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff5fa3d1));

        label.setText (labelText, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredTop);
        label.setColour (juce::Label::textColourId, textColour);
        label.setFont (juce::FontOptions (12.0f));
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
    }

    static void layoutKnob (juce::Label& label,
                            juce::Slider& slider,
                            juce::Rectangle<int> bounds)
    {
        label.setBounds (bounds.removeFromTop (18));
        slider.setBounds (bounds);
    }

    SVDrummerAudioProcessor& processor;
    int padIndex = 0;
    bool updating = false;
    juce::Label titleLabel;
    juce::Label pathLabel;
    juce::Label volumeLabel;
    juce::Label panLabel;
    juce::Label tuneLabel;
    juce::Slider volumeSlider;
    juce::Slider panSlider;
    juce::Slider tuneSlider;
    juce::TextButton reverseButton { "REVERSE" };
    juce::TextButton clearButton { "CLEAR SAMPLE" };
};

class SVDrummerSequencerPlaceholder final : public juce::Component
{
public:
    void paint (juce::Graphics& g) override
    {
        g.setColour (panelColour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (lineColour);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);

        auto area = getLocalBounds().reduced (12);
        auto heading = area.removeFromTop (25);
        g.setColour (textColour);
        g.setFont (14.0f);
        g.drawText ("SEQUENCER FOUNDATION", heading, juce::Justification::centredLeft);
        g.setColour (mutedTextColour);
        g.setFont (11.5f);
        g.drawText ("Independent rate, loop length and velocity engine begins in Stage 2",
                    heading, juce::Justification::centredRight);

        area.removeFromTop (6);
        auto patternArea = area.removeFromBottom (38);
        area.removeFromBottom (7);

        const int rowGap = 2;
        const int rowHeight = juce::jmax (7, (area.getHeight() - 15 * rowGap) / 16);

        for (int row = 0; row < 16; ++row)
        {
            auto rowBounds = area.removeFromTop (rowHeight);
            area.removeFromTop (rowGap);
            auto label = rowBounds.removeFromLeft (34);

            g.setColour (getPadColour (row));
            g.fillRoundedRectangle (label.toFloat(), 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.82f));
            g.setFont (9.0f);
            g.drawText (juce::String (row + 1).paddedLeft ('0', 2), label,
                        juce::Justification::centred);

            rowBounds.removeFromLeft (5);
            const float stepWidth = static_cast<float> (rowBounds.getWidth()) / 16.0f;

            for (int step = 0; step < 16; ++step)
            {
                const auto cell = juce::Rectangle<float> (
                    static_cast<float> (rowBounds.getX()) + stepWidth * static_cast<float> (step),
                    static_cast<float> (rowBounds.getY()),
                    juce::jmax (1.0f, stepWidth - 2.0f),
                    static_cast<float> (rowBounds.getHeight()));
                g.setColour (getPadColour (row).withAlpha (step % 4 == 0 ? 0.18f : 0.09f));
                g.fillRoundedRectangle (cell, 1.5f);
            }
        }

        const float patternWidth = static_cast<float> (patternArea.getWidth()) / 16.0f;

        for (int pattern = 0; pattern < 16; ++pattern)
        {
            const auto button = juce::Rectangle<float> (
                static_cast<float> (patternArea.getX()) + patternWidth * static_cast<float> (pattern),
                static_cast<float> (patternArea.getY()),
                juce::jmax (1.0f, patternWidth - 3.0f),
                static_cast<float> (patternArea.getHeight()));
            g.setColour (pattern == 0 ? juce::Colour (0xff405064) : raisedPanelColour);
            g.fillRoundedRectangle (button, 2.5f);
            g.setColour (lineColour);
            g.drawRoundedRectangle (button, 2.5f, 1.0f);
            g.setColour (pattern == 0 ? textColour : mutedTextColour);
            g.setFont (10.0f);
            g.drawText (juce::String (pattern + 1).paddedLeft ('0', 2),
                        button.toNearestInt(), juce::Justification::centred);
        }
    }
};

SVDrummerAudioProcessorEditor::SVDrummerAudioProcessorEditor (
    SVDrummerAudioProcessor& owner)
    : AudioProcessorEditor (&owner), processor (owner)
{
    lookAndFeel = std::make_unique<SVDrummerLookAndFeel>();
    setLookAndFeel (lookAndFeel.get());
    setOpaque (true);

    browserPanel = std::make_unique<SVDrummerBrowserPanel> (processor);
    addAndMakeVisible (*browserPanel);

    for (int padIndex = 0; padIndex < SVDrummerAudioProcessor::numberOfPads; ++padIndex)
    {
        padComponents[static_cast<std::size_t> (padIndex)]
            = std::make_unique<SVDrummerPadComponent> (
                processor, padIndex,
                [this] (int selected) { showPadSettings (selected); });
        addAndMakeVisible (*padComponents[static_cast<std::size_t> (padIndex)]);
    }

    padSettingsPanel = std::make_unique<SVDrummerPadSettingsPanel> (processor);
    sequencerPlaceholder = std::make_unique<SVDrummerSequencerPlaceholder>();
    addAndMakeVisible (*padSettingsPanel);
    addAndMakeVisible (*sequencerPlaceholder);

    sequencerViewButton.onClick = [this] { showSequencerView(); };
    settingsViewButton.onClick = [this] { showPadSettings (selectedPad); };
    addAndMakeVisible (sequencerViewButton);
    addAndMakeVisible (settingsViewButton);

    showSequencerView();

    setResizable (true, true);
    setResizeLimits (960, 540, 2560, 1440);
    getConstrainer()->setFixedAspectRatio (16.0 / 9.0);
    setSize (1280, 720);
    startTimerHz (20);
}

SVDrummerAudioProcessorEditor::~SVDrummerAudioProcessorEditor()
{
    stopTimer();
    processor.flushPortableSettingsIfNeeded();
    setLookAndFeel (nullptr);
}

void SVDrummerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);

    auto header = getLocalBounds().reduced (10).removeFromTop (42);
    g.setColour (textColour);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("SV-DRUMMER", header, juce::Justification::centredLeft);

    auto version = header.removeFromLeft (175);
    version.removeFromLeft (142);
    g.setColour (juce::Colour (0xff5fa3d1));
    g.setFont (12.0f);
    g.drawText ("v1.0", version, juce::Justification::centredLeft);

    g.setColour (mutedTextColour);
    g.setFont (11.5f);
    g.drawText ("16-PAD SAMPLE PLAYER  •  STAGE 1.0",
                header, juce::Justification::centredRight);
}

void SVDrummerAudioProcessorEditor::resized()
{
    auto area = getLocalBounds().reduced (10);
    area.removeFromTop (48);

    const int browserWidth = juce::jlimit (245, 390,
                                           static_cast<int> (getWidth() * 0.225f));
    browserPanel->setBounds (area.removeFromLeft (browserWidth));
    area.removeFromLeft (10);

    const int gap = juce::jlimit (3, 7, getWidth() / 210);
    const int availablePadWidth = (area.getWidth() - gap * 7) / 8;
    const int maximumPadHeight = juce::jmax (150, static_cast<int> (area.getHeight() * 0.45f));
    const int padSize = juce::jmax (65, juce::jmin (availablePadWidth,
                                                   (maximumPadHeight - gap) / 2));

    auto padGrid = area.removeFromTop (padSize * 2 + gap);

    for (int row = 0; row < 2; ++row)
    {
        for (int column = 0; column < 8; ++column)
        {
            const int padIndex = row * 8 + column;
            padComponents[static_cast<std::size_t> (padIndex)]->setBounds (
                padGrid.getX() + column * (padSize + gap),
                padGrid.getY() + row * (padSize + gap),
                padSize,
                padSize);
        }
    }

    area.removeFromTop (8);
    auto viewButtons = area.removeFromTop (28);
    sequencerViewButton.setBounds (viewButtons.removeFromLeft (132));
    viewButtons.removeFromLeft (5);
    settingsViewButton.setBounds (viewButtons.removeFromLeft (132));
    area.removeFromTop (6);

    sequencerPlaceholder->setBounds (area);
    padSettingsPanel->setBounds (area);
}

void SVDrummerAudioProcessorEditor::timerCallback()
{
    for (auto& pad : padComponents)
        pad->refreshAnimation();

    if (showingSettings)
        padSettingsPanel->syncFromProcessor();

    if (++portableSettingsTimerTicks >= 20)
    {
        portableSettingsTimerTicks = 0;
        processor.flushPortableSettingsIfNeeded();
    }
}

void SVDrummerAudioProcessorEditor::showSequencerView()
{
    showingSettings = false;
    sequencerPlaceholder->setVisible (true);
    padSettingsPanel->setVisible (false);
    updateViewButtons();
}

void SVDrummerAudioProcessorEditor::showPadSettings (int padIndex)
{
    selectedPad = juce::jlimit (0, SVDrummerAudioProcessor::numberOfPads - 1,
                               padIndex);
    showingSettings = true;
    padSettingsPanel->setPadIndex (selectedPad);
    sequencerPlaceholder->setVisible (false);
    padSettingsPanel->setVisible (true);
    updateViewButtons();
}

void SVDrummerAudioProcessorEditor::updateViewButtons()
{
    sequencerViewButton.setColour (juce::TextButton::buttonColourId,
                                   showingSettings ? raisedPanelColour
                                                   : juce::Colour (0xff3e536a));
    settingsViewButton.setColour (juce::TextButton::buttonColourId,
                                  showingSettings ? juce::Colour (0xff3e536a)
                                                  : raisedPanelColour);
    repaint();
}
