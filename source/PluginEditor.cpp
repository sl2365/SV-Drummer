#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
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
         + juce::MidiMessage::getMidiNoteName (note, true, true, 4);
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
    SampleTreeItem (juce::File itemFile,
                    juce::File libraryRoot,
                    bool topLevel,
                    bool patternBrowser)
        : file (std::move (itemFile)),
          rootFolder (std::move (libraryRoot)),
          isTopLevel (topLevel),
          showingPatterns (patternBrowser)
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
        for (const auto& entry : juce::RangedDirectoryIterator (
                 file, false, "*", juce::File::findFilesAndDirectories))
        {
            const auto child = entry.getFile();

            if (entry.isHidden())
                continue;

            if (child.isDirectory())
                directories.add (child);
            else if (isSupportedFile (child))
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
            addSubItem (new SampleTreeItem (
                directory, rootFolder, false, showingPatterns));

        for (const auto& sample : samples)
            addSubItem (new SampleTreeItem (
                sample, rootFolder, false, showingPatterns));
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
    bool isSupportedDragFile() const                 { return isSupportedFile (file); }

private:
    bool isSupportedFile (const juce::File& candidate) const
    {
        return showingPatterns
                 ? SVDrummerAudioProcessor::isSupportedPatternFile (candidate)
                 : SVDrummerAudioProcessor::isSupportedAudioFile (candidate);
    }

    juce::File file;
    juce::File rootFolder;
    bool isTopLevel = false;
    bool showingPatterns = false;
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
            || ! item.isSupportedDragFile())
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

    void rebuild (const juce::StringArray& folders, bool showPatternFiles)
    {
        rootItem.clearSubItems();

        for (const auto& path : folders)
        {
            const juce::File folder (path);

            if (folder.isDirectory())
                rootItem.addSubItem (new SampleTreeItem (
                    folder, folder, true, showPatternFiles));
        }

        rootItem.setOpen (true);

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

        if (showingSamples)
        {
            addFolderButton.setBounds (
                tools.removeFromLeft (tools.getWidth() * 44 / 100).reduced (1));
            removeFolderButton.setBounds (
                tools.removeFromLeft (tools.getWidth() * 30 / 56).reduced (1));
            refreshButton.setBounds (tools.reduced (1));
        }
        else
        {
            refreshButton.setBounds (tools.reduced (1));
        }
        area.removeFromTop (6);

        tree.setBounds (area);
        emptyLabel.setBounds (area.reduced (16));
    }

    void refresh()
    {
        juce::StringArray folders;

        if (showingSamples)
        {
            folders = processor.getBrowserFolders();
            tree.rebuild (folders, false);
            emptyLabel.setVisible (folders.isEmpty());
        }
        else
        {
            const auto patternDirectory = processor.getPortablePatternsDirectory();
            patternDirectory.createDirectory();
            folders.add (patternDirectory.getFullPathName());
            tree.rebuild (folders, true);
            emptyLabel.setVisible (patternDirectory.findChildFiles (
                juce::File::findFiles, true, "*.svpattern").isEmpty());
        }
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

        tree.setVisible (true);
        addFolderButton.setVisible (showingSamples);
        removeFolderButton.setVisible (showingSamples);
        refreshButton.setVisible (true);

        if (! showingSamples)
        {
            emptyLabel.setText ("Saved patterns appear here.\n\n"
                                "Drag a pattern onto any pattern slot.",
                                juce::dontSendNotification);
        }
        else
        {
            emptyLabel.setText ("Add one or more sample folders.\n\n"
                                "Drag supported samples from this tree onto a pad.",
                                juce::dontSendNotification);
        }

        resized();
        refresh();
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

class SVDrummerTransportButton final : public juce::Button
{
public:
    SVDrummerTransportButton() : juce::Button ("Sequencer transport")
    {
        setClickingTogglesState (true);
        setTooltip ("Start or stop the sequencer while the host transport is running");
    }

    void paintButton (juce::Graphics& g,
                      bool highlighted,
                      bool down) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        auto background = getToggleState() ? juce::Colour (0xff3e536a)
                                            : raisedPanelColour;

        if (down)
            background = background.brighter (0.18f);
        else if (highlighted)
            background = background.brighter (0.09f);

        g.setColour (background);
        g.fillRoundedRectangle (bounds, 3.0f);
        g.setColour (lineColour.brighter (highlighted ? 0.22f : 0.0f));
        g.drawRoundedRectangle (bounds, 3.0f, 1.0f);

        const auto icon = bounds.reduced (bounds.getWidth() * 0.31f,
                                          bounds.getHeight() * 0.25f);
        g.setColour (textColour);

        if (getToggleState())
        {
            const float side = juce::jmin (icon.getWidth(), icon.getHeight());
            g.fillRoundedRectangle (
                juce::Rectangle<float> (side, side).withCentre (icon.getCentre()), 1.0f);
        }
        else
        {
            juce::Path triangle;
            triangle.addTriangle (icon.getX(), icon.getY(),
                                  icon.getX(), icon.getBottom(),
                                  icon.getRight(), icon.getCentreY());
            g.fillPath (triangle);
        }
    }
};

class SVDrummerPatternSlot final : public juce::Component,
                                   public juce::SettableTooltipClient,
                                   public juce::FileDragAndDropTarget,
                                   public juce::DragAndDropTarget
{
public:
    SVDrummerPatternSlot (SVDrummerAudioProcessor& owner,
                          int slotIndex,
                          std::function<void()> libraryChanged)
        : processor (owner),
          patternIndex (slotIndex),
          onPatternLibraryChanged (std::move (libraryChanged))
    {
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
        setTooltip ("Click to load pattern; mouse-wheel changes its MIDI note; "
                    "right-click for save and MIDI options");
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat().reduced (0.5f);
        const bool selected = processor.getCurrentPatternIndex() == patternIndex;
        const bool assigned = processor.isPatternAssigned (patternIndex);
        auto background = selected ? juce::Colour (0xff405a73)
                                   : (assigned ? raisedPanelColour
                                               : panelColour.darker (0.12f));

        if (dropHighlight)
            background = background.brighter (0.24f);

        g.setColour (background);
        g.fillRoundedRectangle (bounds, 2.5f);
        g.setColour (selected ? juce::Colour (0xff72b8e5) : lineColour);
        g.drawRoundedRectangle (bounds, 2.5f, selected ? 1.6f : 1.0f);

        auto textArea = getLocalBounds().reduced (2, 1);
        auto numberArea = textArea.removeFromTop (textArea.getHeight() * 56 / 100);
        g.setColour (assigned ? textColour : mutedTextColour.withAlpha (0.55f));
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText (juce::String (patternIndex + 1).paddedLeft ('0', 2),
                    numberArea, juce::Justification::centred);

        const int midiNote = processor.getPatternMidiNote (patternIndex);
        g.setColour (midiNote >= 0 ? mutedTextColour : mutedTextColour.withAlpha (0.48f));
        g.setFont (7.8f);
        g.drawFittedText (
            midiNote >= 0
                ? juce::MidiMessage::getMidiNoteName (midiNote, true, true, 4)
                : juce::String ("OFF"),
            textArea, juce::Justification::centred, 1);

        if (assigned)
        {
            g.setColour (getPadColour (patternIndex).withAlpha (0.88f));
            g.fillEllipse (bounds.getRight() - 4.5f, bounds.getY() + 2.0f, 2.5f, 2.5f);
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (event.mods.isRightButtonDown())
        {
            showMenu();
            return;
        }

        processor.selectPattern (patternIndex);
        repaint();
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY == 0.0f)
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        int note = processor.getPatternMidiNote (patternIndex);

        if (note < 0)
            note = 60 + patternIndex;
        else
            note += wheel.deltaY > 0.0f ? 1 : -1;

        processor.setPatternMidiNote (patternIndex, juce::jlimit (0, 127, note));
        repaint();
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        return files.size() > 0
            && SVDrummerAudioProcessor::isSupportedPatternFile (juce::File (files[0]));
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
            loadPattern (juce::File (files[0]));
    }

    bool isInterestedInDragSource (const SourceDetails& details) override
    {
        return SVDrummerAudioProcessor::isSupportedPatternFile (
            juce::File (details.description.toString()));
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
        loadPattern (juce::File (details.description.toString()));
    }

private:
    void showMenu()
    {
        juce::PopupMenu menu;
        menu.addItem (1, "Save to Pattern Library", true,
                      processor.isPatternAssigned (patternIndex));
        menu.addSeparator();
        menu.addItem (2, "MIDI Note Off", true,
                      processor.getPatternMidiNote (patternIndex) < 0);
        menu.addItem (3, "Use Default MIDI Note");

        juce::Component::SafePointer<SVDrummerPatternSlot> safeThis (this);
        menu.showMenuAsync (
            juce::PopupMenu::Options().withTargetComponent (this),
            [safeThis] (int result)
            {
                if (safeThis == nullptr || result == 0)
                    return;

                if (result == 1)
                    safeThis->saveToLibrary();
                else if (result == 2)
                    safeThis->processor.setPatternMidiNote (
                        safeThis->patternIndex, -1);
                else if (result == 3)
                    safeThis->processor.setPatternMidiNote (
                        safeThis->patternIndex, 60 + safeThis->patternIndex);

                safeThis->repaint();
            });
    }

    void saveToLibrary()
    {
        auto name = juce::File::createLegalFileName (
            processor.getPatternName (patternIndex));

        if (name.isEmpty())
            name = "Pattern " + juce::String (patternIndex + 1).paddedLeft ('0', 2);

        const auto file = processor.getPortablePatternsDirectory()
                                   .getChildFile (name + ".svpattern");
        const auto result = processor.savePatternSlotToFile (patternIndex, file);

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "Could not save the pattern:\n\n" + result.getErrorMessage());
        }
        else if (onPatternLibraryChanged)
        {
            onPatternLibraryChanged();
        }
    }

    void loadPattern (const juce::File& file)
    {
        const auto result = processor.loadPatternIntoSlot (patternIndex, file);

        if (result.failed())
        {
            juce::AlertWindow::showMessageBoxAsync (
                juce::MessageBoxIconType::WarningIcon,
                "SV-Drummer",
                "Could not load the pattern:\n\n" + result.getErrorMessage());
        }

        repaint();
    }

    SVDrummerAudioProcessor& processor;
    int patternIndex = 0;
    std::function<void()> onPatternLibraryChanged;
    bool dropHighlight = false;
};

class SVDrummerSequencerPanel final : public juce::Component
{
public:
    SVDrummerSequencerPanel (SVDrummerAudioProcessor& owner,
                             std::function<void()> patternLibraryChanged)
        : processor (owner), barScroll (false)
    {
        enableButton.onClick = [this]
        {
            processor.setSequencerEnabled (enableButton.getToggleState());
            syncFromProcessor();
        };

        configureSequencerKnob (lengthSlider, 1.0,
                                static_cast<double> (SVDrummerAudioProcessor::maximumPatternBars));
        lengthSlider.onValueChange = [this]
        {
            if (updatingControls)
                return;

            processor.setPatternBars (juce::roundToInt (lengthSlider.getValue()));
            updateScrollRange();
            syncLaneControls();
            repaint();
        };

        configureSequencerKnob (viewSlider, 0.0, 2.0);
        viewSlider.textFromValueFunction = [] (double value)
        {
            const int index = juce::jlimit (0, 2, juce::roundToInt (value));
            return juce::String (index == 0 ? 1 : index == 1 ? 2 : 4);
        };
        viewSlider.onValueChange = [this]
        {
            if (! updatingControls)
            {
                const int index = juce::jlimit (
                    0, 2, juce::roundToInt (viewSlider.getValue()));
                visibleBars = index == 0 ? 1 : index == 1 ? 2 : 4;
                updateScrollRange();
                repaint();
            }
        };

        configureSequencerKnob (
            divisionSlider, 0.0,
            static_cast<double> (SVDrummerAudioProcessor::sequencerDivisionCount - 1));
        divisionSlider.textFromValueFunction = [] (double value)
        {
            return SVDrummerAudioProcessor::getSequencerDivisionName (
                juce::roundToInt (value));
        };
        divisionSlider.onValueChange = [this]
        {
            if (updatingControls)
                return;

            processor.setLaneDivision (
                selectedLane, juce::roundToInt (divisionSlider.getValue()));
            syncLaneControls();
            repaint();
        };

        configureSequencerKnob (loopLengthSlider, 1.0, 16.0);
        loopLengthSlider.onValueChange = [this]
        {
            if (! updatingControls)
            {
                processor.setLaneLoopLength (
                    selectedLane, juce::roundToInt (loopLengthSlider.getValue()));
                repaint();
            }
        };

        configureLabel (lengthLabel, "LENGTH");
        configureLabel (viewLabel, "VIEW");
        configureLabel (divisionLabel, "DIV");
        configureLabel (loopLabel, "LOOP");

        laneButton.setInterceptsMouseClicks (false, false);
        laneButton.setColour (juce::TextButton::buttonColourId,
                              getPadColour (selectedLane).withAlpha (0.58f));

        barScroll.setAutoHide (false);
        barScroll.setSingleStepSize (1.0);
        barScroll.setColour (juce::ScrollBar::backgroundColourId, panelColour);
        barScroll.setColour (juce::ScrollBar::thumbColourId, juce::Colour (0xff46515e));
        barScroll.setColour (juce::ScrollBar::trackColourId, raisedPanelColour);

        addAndMakeVisible (enableButton);
        addAndMakeVisible (lengthLabel);
        addAndMakeVisible (lengthSlider);
        addAndMakeVisible (viewLabel);
        addAndMakeVisible (viewSlider);
        addAndMakeVisible (laneButton);
        addAndMakeVisible (divisionLabel);
        addAndMakeVisible (divisionSlider);
        addAndMakeVisible (loopLabel);
        addAndMakeVisible (loopLengthSlider);
        addAndMakeVisible (barScroll);

        for (int patternIndex = 0;
             patternIndex < SVDrummerAudioProcessor::numberOfPatterns;
             ++patternIndex)
        {
            patternSlots[static_cast<std::size_t> (patternIndex)]
                = std::make_unique<SVDrummerPatternSlot> (
                    processor, patternIndex, patternLibraryChanged);
            addAndMakeVisible (*patternSlots[static_cast<std::size_t> (patternIndex)]);
        }

        updatingControls = true;
        viewSlider.setValue (0.0, juce::dontSendNotification);
        updatingControls = false;
        syncFromProcessor();
        setMouseCursor (juce::MouseCursor::PointingHandCursor);
    }

    void refresh()
    {
        syncFromProcessor();

        for (auto& slot : patternSlots)
            slot->repaint();

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        g.setColour (panelColour);
        g.fillRoundedRectangle (getLocalBounds().toFloat(), 5.0f);
        g.setColour (lineColour);
        g.drawRoundedRectangle (getLocalBounds().toFloat().reduced (0.5f), 5.0f, 1.0f);

        auto area = getLocalBounds().reduced (10);
        auto heading = area.removeFromTop (20);
        g.setColour (textColour);
        g.setFont (juce::FontOptions (13.0f, juce::Font::bold));
        g.drawText ("SEQUENCER", heading, juce::Justification::centredLeft);
        g.setColour (mutedTextColour);
        g.setFont (10.5f);
        g.drawFittedText ("Click/drag steps  -  mouse wheel velocity  -  right-drag clear",
                          heading, juce::Justification::centredRight, 1);

        if (! rulerBounds.isEmpty())
        {
            auto rulerSteps = rulerBounds;
            auto rulerLabel = rulerSteps.removeFromLeft (laneLabelWidth);
            rulerSteps.removeFromLeft (4);
            const int startBar = getStartBar();
            const int barsShown = getBarsShown();
            const float barWidth = static_cast<float> (rulerSteps.getWidth())
                                 / static_cast<float> (juce::jmax (1, barsShown));

            g.setColour (mutedTextColour);
            g.setFont (8.5f);
            g.drawText ("LANE", rulerLabel, juce::Justification::centredLeft);

            for (int bar = 0; bar < barsShown; ++bar)
            {
                auto barBounds = juce::Rectangle<float> (
                    static_cast<float> (rulerSteps.getX()) + barWidth * static_cast<float> (bar),
                    static_cast<float> (rulerSteps.getY()), barWidth,
                    static_cast<float> (rulerSteps.getHeight()));
                g.drawText ("BAR " + juce::String (startBar + bar + 1),
                            barBounds.toNearestInt(), juce::Justification::centred);
            }
        }

        drawSequenceRows (g);
        drawPlayhead (g);

        auto patternArea = patternBounds;
        auto patternLabel = patternArea.removeFromLeft (laneLabelWidth);
        g.setColour (mutedTextColour);
        g.setFont (8.5f);
        g.drawText ("PATTERN", patternLabel, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);
        area.removeFromTop (20);

        auto controls = area.removeFromTop (66);
        const int controlY = controls.getY() + (controls.getHeight() - 27) / 2;
        enableButton.setBounds (controls.removeFromLeft (42).withY (controlY).withHeight (27));
        controls.removeFromLeft (8);

        auto placeKnob = [&controls] (juce::Label& label, juce::Slider& slider)
        {
            auto group = controls.removeFromLeft (58);
            label.setBounds (group.removeFromTop (13));
            slider.setBounds (group);
            controls.removeFromLeft (4);
        };

        placeKnob (lengthLabel, lengthSlider);
        placeKnob (viewLabel, viewSlider);
        laneButton.setBounds (controls.removeFromLeft (72).withY (controlY).withHeight (27));
        controls.removeFromLeft (8);
        placeKnob (divisionLabel, divisionSlider);
        placeKnob (loopLabel, loopLengthSlider);

        area.removeFromTop (5);
        patternBounds = area.removeFromBottom (30);
        area.removeFromBottom (4);
        barScroll.setBounds (area.removeFromBottom (12));
        area.removeFromBottom (3);
        rulerBounds = area.removeFromTop (13);
        sequenceRowsBounds = area;

        auto slotsArea = patternBounds;
        slotsArea.removeFromLeft (laneLabelWidth + 4);
        const float slotWidth = static_cast<float> (slotsArea.getWidth())
                              / static_cast<float> (SVDrummerAudioProcessor::numberOfPatterns);

        for (int patternIndex = 0;
             patternIndex < SVDrummerAudioProcessor::numberOfPatterns;
             ++patternIndex)
        {
            const int left = juce::roundToInt (
                static_cast<float> (slotsArea.getX())
                + slotWidth * static_cast<float> (patternIndex));
            const int right = juce::roundToInt (
                static_cast<float> (slotsArea.getX())
                + slotWidth * static_cast<float> (patternIndex + 1));
            patternSlots[static_cast<std::size_t> (patternIndex)]->setBounds (
                left, slotsArea.getY(), juce::jmax (1, right - left - 2),
                slotsArea.getHeight());
        }
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        stepGestureActive = false;
        gestureLane = -1;
        lastGestureVisibleStep = -1;

        if (! sequenceRowsBounds.contains (event.getPosition()))
            return;

        const int lane = laneAt (event.position.y);

        if (lane < 0)
            return;

        selectLane (lane);

        const int visibleStep = visibleStepAt (event.position.x, lane);

        if (visibleStep < 0)
            return;

        const int dataStep = dataStepForVisibleStep (lane, visibleStep);
        const int currentVelocity = processor.getSequenceStepVelocity (lane, dataStep);
        gestureLane = lane;
        gestureVelocity = event.mods.isRightButtonDown()
                            ? 0
                            : (currentVelocity > 0 ? 0 : lastDrawVelocity);
        lastGestureVisibleStep = visibleStep;
        stepGestureActive = true;
        setVisibleStepVelocity (gestureLane, visibleStep, gestureVelocity);
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (! stepGestureActive || gestureLane < 0)
            return;

        const int visibleStep = visibleStepAt (event.position.x, gestureLane);

        if (visibleStep < 0 || visibleStep == lastGestureVisibleStep)
            return;

        const int first = juce::jmin (lastGestureVisibleStep, visibleStep);
        const int last = juce::jmax (lastGestureVisibleStep, visibleStep);

        for (int step = first; step <= last; ++step)
            setVisibleStepVelocity (gestureLane, step, gestureVelocity);

        lastGestureVisibleStep = visibleStep;
        repaint();
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        stepGestureActive = false;
        gestureLane = -1;
        lastGestureVisibleStep = -1;
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        if (wheel.deltaY == 0.0f || ! sequenceRowsBounds.contains (event.getPosition()))
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        const int lane = laneAt (event.position.y);
        const int visibleStep = visibleStepAt (event.position.x, lane);

        if (lane < 0 || visibleStep < 0)
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        const int dataStep = dataStepForVisibleStep (lane, visibleStep);
        const int currentVelocity = processor.getSequenceStepVelocity (lane, dataStep);

        if (currentVelocity <= 0)
            return;

        const int newVelocity = juce::jlimit (
            1, 127, currentVelocity + (wheel.deltaY > 0.0f ? 5 : -5));
        processor.setSequenceStepVelocity (lane, dataStep, newVelocity);
        lastDrawVelocity = newVelocity;
        selectLane (lane);
        repaint();
    }

private:
    void configureSequencerKnob (juce::Slider& slider,
                                 double minimum,
                                 double maximum)
    {
        slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 52, 14);
        slider.setNumDecimalPlacesToDisplay (0);
        slider.setRange (minimum, maximum, 1.0);
        slider.setMouseDragSensitivity (80);
        slider.setColour (juce::Slider::rotarySliderFillColourId,
                          juce::Colour (0xff5fa3d1));
        slider.setColour (juce::Slider::textBoxTextColourId, textColour);
        slider.setColour (juce::Slider::textBoxBackgroundColourId,
                          juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId,
                          juce::Colours::transparentBlack);
    }

    void configureLabel (juce::Label& label, const juce::String& text)
    {
        label.setText (text, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centred);
        label.setColour (juce::Label::textColourId, mutedTextColour);
        label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
    }

    void syncFromProcessor()
    {
        updatingControls = true;
        const bool enabled = processor.isSequencerEnabled();
        enableButton.setToggleState (enabled, juce::dontSendNotification);
        enableButton.repaint();
        lengthSlider.setValue (processor.getPatternBars(), juce::dontSendNotification);
        updatingControls = false;
        updateScrollRange();
        syncLaneControls();
    }

    void syncLaneControls()
    {
        updatingControls = true;
        const int division = processor.getLaneDivision (selectedLane);
        divisionSlider.setValue (division, juce::dontSendNotification);
        loopLengthSlider.setRange (1.0,
                                   static_cast<double> (
                                       processor.getLaneMaximumLoopLength (selectedLane)),
                                   1.0);
        loopLengthSlider.setValue (processor.getLaneLoopLength (selectedLane),
                                   juce::dontSendNotification);
        laneButton.setButtonText ("LANE "
                                  + juce::String (selectedLane + 1).paddedLeft ('0', 2));
        laneButton.setColour (juce::TextButton::buttonColourId,
                              getPadColour (selectedLane).withAlpha (0.58f));
        updatingControls = false;
    }

    void updateScrollRange()
    {
        const double totalBars = static_cast<double> (processor.getPatternBars());
        const double shown = static_cast<double> (getBarsShown());
        const double oldStart = barScroll.getCurrentRangeStart();
        const double maximumStart = juce::jmax (0.0, totalBars - shown);
        barScroll.setRangeLimits (0.0, totalBars, juce::dontSendNotification);
        barScroll.setCurrentRange (juce::jlimit (0.0, maximumStart, oldStart),
                                   shown, juce::dontSendNotification);
    }

    int getBarsShown() const
    {
        return juce::jmin (visibleBars, processor.getPatternBars());
    }

    int getStartBar() const
    {
        return juce::jlimit (0,
                             juce::jmax (0, processor.getPatternBars() - getBarsShown()),
                             juce::roundToInt (barScroll.getCurrentRangeStart()));
    }

    int laneAt (float y) const
    {
        if (sequenceRowsBounds.isEmpty())
            return -1;

        const float relativeY = y - static_cast<float> (sequenceRowsBounds.getY());
        const float rowHeight = static_cast<float> (sequenceRowsBounds.getHeight()) / 16.0f;
        return juce::jlimit (0, 15, static_cast<int> (std::floor (relativeY / rowHeight)));
    }

    void selectLane (int lane)
    {
        const int newLane = juce::jlimit (0, 15, lane);

        if (newLane != selectedLane)
        {
            selectedLane = newLane;
            syncLaneControls();
            repaint();
        }
    }

    int visibleStepAt (float x, int lane) const
    {
        if (lane < 0 || lane >= 16)
            return -1;

        auto stepsBounds = sequenceRowsBounds;
        stepsBounds.removeFromLeft (laneLabelWidth + 4);

        if (x < static_cast<float> (stepsBounds.getX())
            || x >= static_cast<float> (stepsBounds.getRight()))
            return -1;

        const int division = processor.getLaneDivision (lane);
        const int stepsPerBar = SVDrummerAudioProcessor::getSequencerStepsPerBar (division);
        const int visibleSteps = juce::jmax (1, getBarsShown() * stepsPerBar);
        const float relativeX = x - static_cast<float> (stepsBounds.getX());
        return juce::jlimit (
            0, visibleSteps - 1,
            static_cast<int> (std::floor (
                relativeX * static_cast<float> (visibleSteps)
                / static_cast<float> (juce::jmax (1, stepsBounds.getWidth())))));
    }

    int dataStepForVisibleStep (int lane, int visibleStep) const
    {
        const int division = processor.getLaneDivision (lane);
        const int stepsPerBar = SVDrummerAudioProcessor::getSequencerStepsPerBar (division);
        const int globalStep = getStartBar() * stepsPerBar + visibleStep;
        const int loopLength = juce::jmax (1, processor.getLaneLoopLength (lane));
        return globalStep % loopLength;
    }

    void setVisibleStepVelocity (int lane, int visibleStep, int velocity)
    {
        processor.setSequenceStepVelocity (
            lane, dataStepForVisibleStep (lane, visibleStep), velocity);
        repaint();
    }

    void drawSequenceRows (juce::Graphics& g)
    {
        if (sequenceRowsBounds.isEmpty())
            return;

        const float rowHeight = static_cast<float> (sequenceRowsBounds.getHeight()) / 16.0f;
        const int startBar = getStartBar();
        const int barsShown = getBarsShown();
        for (int lane = 0; lane < 16; ++lane)
        {
            const float rowY = static_cast<float> (sequenceRowsBounds.getY())
                             + rowHeight * static_cast<float> (lane);
            auto row = juce::Rectangle<float> (
                static_cast<float> (sequenceRowsBounds.getX()), rowY,
                static_cast<float> (sequenceRowsBounds.getWidth()), rowHeight);
            auto label = row.removeFromLeft (static_cast<float> (laneLabelWidth));
            row.removeFromLeft (4.0f);
            const auto accent = getPadColour (lane);

            g.setColour (lane == selectedLane ? accent.withAlpha (0.72f)
                                              : accent.withAlpha (0.38f));
            g.fillRoundedRectangle (label.reduced (0.0f, 0.6f), 1.5f);
            g.setColour (juce::Colours::white.withAlpha (0.90f));
            g.setFont (juce::jmax (7.0f, juce::jmin (9.0f, rowHeight * 0.68f)));
            g.drawFittedText (
                juce::String (lane + 1).paddedLeft ('0', 2) + "  "
                    + SVDrummerAudioProcessor::getSequencerDivisionName (
                        processor.getLaneDivision (lane)),
                label.toNearestInt().reduced (3, 0),
                juce::Justification::centredLeft, 1);

            const int division = processor.getLaneDivision (lane);
            const int stepsPerBar = SVDrummerAudioProcessor::getSequencerStepsPerBar (division);
            const int visibleSteps = juce::jmax (1, barsShown * stepsPerBar);
            const int loopLength = juce::jmax (1, processor.getLaneLoopLength (lane));
            const float cellWidth = row.getWidth() / static_cast<float> (visibleSteps);
            for (int visibleStep = 0; visibleStep < visibleSteps; ++visibleStep)
            {
                const int globalStep = startBar * stepsPerBar + visibleStep;
                const int dataStep = globalStep % loopLength;
                const int velocity = processor.getSequenceStepVelocity (lane, dataStep);
                const bool repeatedOccurrence = globalStep >= loopLength;
                const float repeatScale = repeatedOccurrence ? 0.34f : 1.0f;
                auto cell = juce::Rectangle<float> (
                    row.getX() + cellWidth * static_cast<float> (visibleStep),
                    row.getY(), juce::jmax (0.75f, cellWidth - 0.7f), row.getHeight());
                auto inner = cell.reduced (0.0f, 0.7f);

                const float backgroundAlpha = dataStep == 0 ? 0.20f : 0.10f;
                g.setColour (accent.withAlpha (backgroundAlpha * repeatScale));
                g.fillRoundedRectangle (inner, 1.0f);

                if (velocity > 0)
                {
                    const float amount = static_cast<float> (velocity) / 127.0f;
                    auto velocityFill = inner;
                    velocityFill.setTop (velocityFill.getBottom()
                                         - velocityFill.getHeight() * amount);
                    g.setColour (accent.withAlpha (
                        (0.48f + amount * 0.48f) * repeatScale));
                    g.fillRoundedRectangle (velocityFill, 1.0f);
                }

                if (visibleStep > 0 && visibleStep % stepsPerBar == 0)
                {
                    g.setColour (lineColour.brighter (0.32f));
                    g.drawVerticalLine (juce::roundToInt (cell.getX()),
                                        row.getY(), row.getBottom());
                }
                else if (dataStep == 0 && globalStep > 0)
                {
                    g.setColour (accent.withAlpha (repeatedOccurrence ? 0.26f : 0.50f));
                    g.drawVerticalLine (juce::roundToInt (cell.getX()),
                                        row.getY(), row.getBottom());
                }
            }
        }
    }

    void drawPlayhead (juce::Graphics& g)
    {
        if (! processor.isSequencerEnabled()
            || processor.getActiveSequenceStep (0) < 0
            || sequenceRowsBounds.isEmpty())
            return;

        auto stepArea = sequenceRowsBounds;
        stepArea.removeFromLeft (laneLabelWidth + 4);
        const double visibleStart = static_cast<double> (getStartBar()) * 4.0;
        const double visibleLength = static_cast<double> (getBarsShown()) * 4.0;
        const double position = processor.getSequencerPatternPositionQuarterNotes();

        if (position < visibleStart || position >= visibleStart + visibleLength)
            return;

        const int sixteenthColumns = juce::jmax (1, getBarsShown() * 16);
        const int column = juce::jlimit (
            0, sixteenthColumns - 1,
            static_cast<int> (std::floor ((position - visibleStart) / 0.25)));
        const float columnWidth = static_cast<float> (stepArea.getWidth())
                                / static_cast<float> (sixteenthColumns);
        auto playhead = juce::Rectangle<float> (
            static_cast<float> (stepArea.getX())
                + columnWidth * static_cast<float> (column),
            static_cast<float> (stepArea.getY()),
            columnWidth,
            static_cast<float> (stepArea.getHeight()));

        g.setColour (juce::Colours::white.withAlpha (0.055f));
        g.fillRect (playhead);
        g.setColour (juce::Colours::white.withAlpha (0.68f));
        g.drawRect (playhead.reduced (0.45f), 1.15f);
    }

    SVDrummerAudioProcessor& processor;
    SVDrummerTransportButton enableButton;
    juce::Label lengthLabel;
    juce::Slider lengthSlider;
    juce::Label viewLabel;
    juce::Slider viewSlider;
    juce::TextButton laneButton { "LANE 01" };
    juce::Label divisionLabel;
    juce::Slider divisionSlider;
    juce::Label loopLabel;
    juce::Slider loopLengthSlider;
    juce::ScrollBar barScroll;
    std::array<std::unique_ptr<SVDrummerPatternSlot>,
               SVDrummerAudioProcessor::numberOfPatterns> patternSlots;
    juce::Rectangle<int> rulerBounds;
    juce::Rectangle<int> sequenceRowsBounds;
    juce::Rectangle<int> patternBounds;
    int selectedLane = 0;
    int visibleBars = 1;
    int laneLabelWidth = 62;
    int gestureLane = -1;
    int gestureVelocity = 0;
    int lastGestureVisibleStep = -1;
    int lastDrawVelocity = 100;
    bool stepGestureActive = false;
    bool updatingControls = false;
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
    sequencerPanel = std::make_unique<SVDrummerSequencerPanel> (
        processor,
        [this]
        {
            if (browserPanel != nullptr)
                browserPanel->refresh();
        });
    addAndMakeVisible (*padSettingsPanel);
    addAndMakeVisible (*sequencerPanel);

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
    auto title = header.removeFromLeft (205);
    g.setColour (textColour);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("SV-DRUMMER", title.removeFromLeft (148),
                juce::Justification::centredLeft);

    g.setColour (juce::Colour (0xff5fa3d1));
    g.setFont (12.0f);
    g.drawText ("v1.0", title, juce::Justification::centredLeft);

    g.setColour (mutedTextColour);
    g.setFont (11.5f);
    g.drawFittedText ("16-PAD SAMPLE PLAYER  -  STAGE 3.1",
                      header, juce::Justification::centredRight, 1);
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

    sequencerPanel->setBounds (area);
    padSettingsPanel->setBounds (area);
}

void SVDrummerAudioProcessorEditor::timerCallback()
{
    for (auto& pad : padComponents)
        pad->refreshAnimation();

    if (showingSettings)
        padSettingsPanel->syncFromProcessor();
    else
        sequencerPanel->refresh();

    if (++portableSettingsTimerTicks >= 20)
    {
        portableSettingsTimerTicks = 0;
        processor.flushPortableSettingsIfNeeded();
    }
}

void SVDrummerAudioProcessorEditor::showSequencerView()
{
    showingSettings = false;
    sequencerPanel->setVisible (true);
    padSettingsPanel->setVisible (false);
    updateViewButtons();
}

void SVDrummerAudioProcessorEditor::showPadSettings (int padIndex)
{
    selectedPad = juce::jlimit (0, SVDrummerAudioProcessor::numberOfPads - 1,
                               padIndex);
    showingSettings = true;
    padSettingsPanel->setPadIndex (selectedPad);
    sequencerPanel->setVisible (false);
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
