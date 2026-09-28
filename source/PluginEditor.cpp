#include "PluginEditor.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <utility>
#include <vector>

namespace
{
const juce::Colour backgroundColour (0xff111316);
const juce::Colour panelColour (0xff191c20);
const juce::Colour raisedPanelColour (0xff20242a);
const juce::Colour lineColour (0xff343940);
const juce::Colour textColour (0xffe8ebef);
const juce::Colour mutedTextColour (0xff8d949e);
constexpr int baseEditorWidth = 1280;
constexpr int baseEditorHeight = 800;

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

        const bool connected = button.isConnectedOnLeft()
                            || button.isConnectedOnRight()
                            || button.isConnectedOnTop()
                            || button.isConnectedOnBottom();

        if (connected)
        {
            const bool curveTopLeft = ! button.isConnectedOnLeft()
                                       && ! button.isConnectedOnTop();
            const bool curveTopRight = ! button.isConnectedOnRight()
                                        && ! button.isConnectedOnTop();
            juce::Path shape;
            shape.addRoundedRectangle (
                bounds.getX(), bounds.getY(), bounds.getWidth(), bounds.getHeight(),
                3.0f, 3.0f,
                curveTopLeft,
                curveTopRight,
                ! button.isConnectedOnLeft() && ! button.isConnectedOnBottom(),
                ! button.isConnectedOnRight() && ! button.isConnectedOnBottom());
            g.setColour (colour);
            g.fillPath (shape);
            g.setColour (lineColour.brighter (highlighted ? 0.22f : 0.0f));

            if (button.getToggleState() && button.isConnectedOnBottom())
            {
                const float radius = 3.0f;
                juce::Path outline;
                outline.startNewSubPath (bounds.getX(), bounds.getBottom());

                if (curveTopLeft)
                {
                    outline.lineTo (bounds.getX(), bounds.getY() + radius);
                    outline.quadraticTo (bounds.getX(), bounds.getY(),
                                         bounds.getX() + radius, bounds.getY());
                }
                else
                {
                    outline.lineTo (bounds.getX(), bounds.getY());
                }

                if (curveTopRight)
                {
                    outline.lineTo (bounds.getRight() - radius, bounds.getY());
                    outline.quadraticTo (bounds.getRight(), bounds.getY(),
                                         bounds.getRight(), bounds.getY() + radius);
                }
                else
                {
                    outline.lineTo (bounds.getRight(), bounds.getY());
                }

                outline.lineTo (bounds.getRight(), bounds.getBottom());
                g.strokePath (outline, juce::PathStrokeType (1.0f));
            }
            else
            {
                g.strokePath (shape, juce::PathStrokeType (1.0f));
            }

            return;
        }

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

    int getItemHeight() const override
    {
        return 24;
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

        const auto iconBounds = juce::Rectangle<float> (
            3.0f, (static_cast<float> (height) - 14.0f) * 0.5f, 14.0f, 14.0f);
        g.setColour (directory ? juce::Colour (0xffd09c50) : juce::Colour (0xff6c7887));

        if (directory)
        {
            g.fillRoundedRectangle (iconBounds, 1.5f);
            g.fillRect (juce::Rectangle<float> (
                5.0f, iconBounds.getY() - 2.0f, 7.0f, 4.0f));
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
        g.setFont (juce::FontOptions (isTopLevel ? 14.5f : 14.0f,
                                     isTopLevel ? juce::Font::bold
                                                : juce::Font::plain));
        g.drawText (file.getFileName(), 23, 0, width - 25, height,
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
        setIndentSize (16);
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

    juce::File getSelectedPreviewFile() const
    {
        if (auto* item = dynamic_cast<SampleTreeItem*> (getSelectedItem (0)))
        {
            const auto selected = item->getFile();

            if (selected.existsAsFile()
                && SVDrummerAudioProcessor::isSupportedAudioFile (selected))
                return selected;
        }

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
        previewButton.onClick = [this]
        {
            const auto selected = tree.getSelectedPreviewFile();

            if (! selected.existsAsFile())
            {
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::InfoIcon,
                    "SV-Drummer",
                    "Select an audio sample in the browser first.");
                return;
            }

            const auto result = processor.previewSampleFile (selected);

            if (result.failed())
                juce::AlertWindow::showMessageBoxAsync (
                    juce::MessageBoxIconType::WarningIcon,
                    "SV-Drummer",
                    "Could not preview the sample:\n\n" + result.getErrorMessage());
        };
        previewButton.setTooltip ("Play the selected sample without loading it onto a pad");

        addAndMakeVisible (samplesButton);
        addAndMakeVisible (patternsButton);
        addAndMakeVisible (addFolderButton);
        addAndMakeVisible (removeFolderButton);
        addAndMakeVisible (refreshButton);
        addAndMakeVisible (previewButton);
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
            const int buttonWidth = tools.getWidth() / 4;
            addFolderButton.setBounds (tools.removeFromLeft (buttonWidth).reduced (1));
            removeFolderButton.setBounds (tools.removeFromLeft (buttonWidth).reduced (1));
            refreshButton.setBounds (tools.removeFromLeft (buttonWidth).reduced (1));
            previewButton.setBounds (tools.reduced (1));
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
        previewButton.setVisible (showingSamples);

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
    juce::TextButton previewButton { "PREVIEW" };
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

        auto titleRow = getLocalBounds().reduced (7).removeFromTop (22);
        auto indicatorBounds = titleRow.removeFromRight (22).toFloat()
                                       .withSizeKeepingCentre (20.0f, 20.0f);
        titleRow.removeFromRight (4);

        const auto sampleName = processor.getPadDisplayName (padIndex);
        g.setColour (muted ? mutedTextColour : textColour);
        g.setFont (juce::jmax (10.0f, static_cast<float> (getHeight()) * 0.095f));
        g.drawText (sampleName, titleRow,
                    juce::Justification::centredLeft, true);

        if (editingSelected)
        {
            g.setColour (accent);
            g.fillEllipse (indicatorBounds);
        }
        else
        {
            g.setColour (juce::Colour (0xff15181c));
            g.fillEllipse (indicatorBounds);
        }

        g.setColour (accent.withAlpha (editingSelected ? 1.0f : 0.72f));
        g.drawEllipse (indicatorBounds.reduced (0.6f), editingSelected ? 1.5f : 1.1f);
        g.setColour (editingSelected ? juce::Colours::white
                                     : mutedTextColour.brighter (0.08f));
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (juce::String (padIndex + 1), indicatorBounds.toNearestInt(),
                    juce::Justification::centred, false);

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

    void setEditingSelected (bool shouldBeSelected)
    {
        if (editingSelected != shouldBeSelected)
        {
            editingSelected = shouldBeSelected;
            repaint();
        }
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
        waveformBounds.removeFromTop (22);
        waveformBounds.removeFromBottom (getFooterBounds().getHeight() - 4);

        if (sample == nullptr || sample->waveform.empty())
        {
            g.setColour (mutedTextColour.withAlpha (0.55f));
            g.setFont (juce::FontOptions (11.5f, juce::Font::bold));
            g.drawText ("DROP SAMPLE", waveformBounds,
                        juce::Justification::centred, false);
            return;
        }

        const float middle = static_cast<float> (waveformBounds.getCentreY());
        const float halfHeight = static_cast<float> (waveformBounds.getHeight()) * 0.45f;
        const float left = static_cast<float> (waveformBounds.getX());
        const float width = static_cast<float> (waveformBounds.getWidth());
        const int points = static_cast<int> (sample->waveform.size());
        const int columns = juce::jmax (1, juce::jmin (waveformBounds.getWidth(), points));

        g.setColour (colour.withAlpha (0.94f));

        for (int column = 0; column < columns; ++column)
        {
            const int firstPoint = column * points / columns;
            const int lastPoint = juce::jmax (
                firstPoint + 1, (column + 1) * points / columns);
            float minimum = 1.0f;
            float maximum = -1.0f;

            for (int point = firstPoint; point < juce::jmin (points, lastPoint); ++point)
            {
                const auto range = sample->waveform[static_cast<std::size_t> (point)];
                minimum = juce::jmin (minimum, range.getStart());
                maximum = juce::jmax (maximum, range.getEnd());
            }

            const float x = left + width * static_cast<float> (column)
                                   / static_cast<float> (juce::jmax (1, columns - 1));
            g.drawVerticalLine (juce::roundToInt (x),
                                middle - maximum * halfHeight,
                                middle - minimum * halfHeight);
        }

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

        g.setFont (juce::FontOptions (
            juce::jmax (10.5f, footer.getHeight() * 0.44f), juce::Font::bold));
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
        const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.26f;
        g.setColour (juce::Colours::white.withAlpha (0.72f));
        g.drawEllipse (centre.x - radius, centre.y - radius,
                       radius * 2.0f, radius * 2.0f, 1.5f);
        g.fillEllipse (centre.x - 1.8f, centre.y - 1.8f, 3.6f, 3.6f);

        for (int tooth = 0; tooth < 8; ++tooth)
        {
            const float angle = juce::MathConstants<float>::twoPi
                              * static_cast<float> (tooth) / 8.0f;
            g.drawLine (centre.x + std::sin (angle) * radius,
                        centre.y - std::cos (angle) * radius,
                        centre.x + std::sin (angle) * radius * 1.38f,
                        centre.y - std::cos (angle) * radius * 1.38f,
                        1.4f);
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
    bool editingSelected = false;
};

class SVDrummerWaveformEditor final : public juce::Component,
                                      public juce::SettableTooltipClient
{
public:
    explicit SVDrummerWaveformEditor (SVDrummerAudioProcessor& owner)
        : processor (owner)
    {
        setTooltip ("Click the waveform to audition; drag an S/E marker; "
                    "use the mouse wheel to zoom; drag the overview bar to "
                    "scroll; double-click to reset the sample range and view");
    }

    void setPadIndex (int newPadIndex)
    {
        const int newIndex = juce::jlimit (
            0, SVDrummerAudioProcessor::numberOfPads - 1, newPadIndex);

        if (padIndex != newIndex)
        {
            padIndex = newIndex;
            resetView();
            displayedSample.reset();
        }

        repaint();
    }

    void paint (juce::Graphics& g) override
    {
        auto bounds = getLocalBounds().toFloat();
        g.setColour (raisedPanelColour);
        g.fillRoundedRectangle (bounds, 4.0f);
        g.setColour (lineColour);
        g.drawRoundedRectangle (bounds.reduced (0.5f), 4.0f, 1.0f);

        const float sampleStart = processor.getPadSampleStart (padIndex);
        const float sampleEnd = processor.getPadSampleEnd (padIndex);
        auto header = getLocalBounds().reduced (8).removeFromTop (18);
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.setColour (textColour);
        g.drawText ("START  " + juce::String (sampleStart * 100.0f, 1) + "%",
                    header.removeFromLeft (header.getWidth() / 2),
                    juce::Justification::centredLeft);
        g.drawText ("END  " + juce::String (sampleEnd * 100.0f, 1) + "%",
                    header, juce::Justification::centredRight);

        const auto waveformBounds = getWaveformBounds();
        const auto overviewBounds = getOverviewBounds();
        const auto sample = processor.getPadSample (padIndex);

        if (sample != displayedSample)
        {
            displayedSample = sample;
            resetView();
        }

        if (sample == nullptr || sample->waveform.empty() || waveformBounds.isEmpty())
        {
            g.setColour (mutedTextColour);
            g.setFont (12.0f);
            g.drawText ("NO SAMPLE LOADED", waveformBounds,
                        juce::Justification::centred);
            return;
        }

        const auto accent = getPadColour (padIndex);
        const float visibleSelectionStart = juce::jmax (viewStart, sampleStart);
        const float visibleSelectionEnd = juce::jmin (viewEnd, sampleEnd);

        if (visibleSelectionEnd > visibleSelectionStart)
        {
            const float selectedStartX = samplePositionToX (
                visibleSelectionStart, waveformBounds);
            const float selectedEndX = samplePositionToX (
                visibleSelectionEnd, waveformBounds);
            g.setColour (accent.withAlpha (0.09f));
            g.fillRect (juce::Rectangle<float> (
                selectedStartX, waveformBounds.getY(),
                juce::jmax (0.0f, selectedEndX - selectedStartX),
                waveformBounds.getHeight()));
        }

        g.setColour (lineColour.withAlpha (0.34f));

        for (int guide = 1; guide < 4; ++guide)
        {
            const float x = waveformBounds.getX()
                          + waveformBounds.getWidth() * static_cast<float> (guide) / 4.0f;
            g.drawVerticalLine (juce::roundToInt (x),
                                waveformBounds.getY(), waveformBounds.getBottom());
        }

        drawFilledWaveform (g, *sample, waveformBounds,
                            viewStart, viewEnd, accent, 0.42f, 0.94f);

        g.setColour (backgroundColour.withAlpha (0.58f));

        if (sampleStart > viewStart)
            g.fillRect (waveformBounds.withRight (
                juce::jmin (waveformBounds.getRight(),
                            samplePositionToX (sampleStart, waveformBounds))));

        if (sampleEnd < viewEnd)
            g.fillRect (waveformBounds.withLeft (
                juce::jmax (waveformBounds.getX(),
                            samplePositionToX (sampleEnd, waveformBounds))));

        g.setColour (juce::Colours::white.withAlpha (0.42f));
        g.drawLine (waveformBounds.getX(), waveformBounds.getCentreY(),
                    waveformBounds.getRight(), waveformBounds.getCentreY(),
                    1.15f);

        constexpr float flagWidth = 15.0f;
        constexpr float flagHeight = 14.0f;
        const auto drawMarker = [&] (float samplePosition,
                                     const juce::String& text,
                                     bool startMarker)
        {
            if (samplePosition < viewStart || samplePosition > viewEnd)
                return;

            const float markerX = samplePositionToX (samplePosition, waveformBounds);
            const float flagX = startMarker
                                  ? juce::jmin (markerX,
                                                waveformBounds.getRight() - flagWidth)
                                  : juce::jmax (waveformBounds.getX(),
                                                markerX - flagWidth);
            const auto flag = juce::Rectangle<float> (
                flagX, waveformBounds.getBottom() - flagHeight,
                flagWidth, flagHeight);
            g.setColour (accent.brighter (0.30f));
            g.drawVerticalLine (juce::roundToInt (markerX),
                                waveformBounds.getY(), waveformBounds.getBottom());
            g.fillRoundedRectangle (flag, 2.0f);
            g.setColour (juce::Colours::white.withAlpha (0.96f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
            g.drawText (text, flag, juce::Justification::centred);
        };

        drawMarker (sampleStart, "S", true);
        drawMarker (sampleEnd, "E", false);

        g.setColour (backgroundColour.darker (0.18f));
        g.fillRoundedRectangle (overviewBounds, 2.0f);
        g.setColour (lineColour.withAlpha (0.64f));
        g.drawRoundedRectangle (overviewBounds.reduced (0.5f), 2.0f, 1.0f);
        drawFilledWaveform (g, *sample, overviewBounds.reduced (2.0f),
                            0.0f, 1.0f,
                            mutedTextColour.brighter (0.15f), 0.35f, 0.62f);

        const auto viewport = getViewportBounds();
        g.setColour (accent.withAlpha (0.12f));
        g.fillRect (viewport);
        g.setColour (accent.withAlpha (0.92f));
        g.drawRect (viewport, 1.2f);
        g.setColour (juce::Colours::white.withAlpha (0.46f));
        g.drawLine (overviewBounds.getX(), overviewBounds.getCentreY(),
                    overviewBounds.getRight(), overviewBounds.getCentreY(),
                    1.0f);

        constexpr float handleWidth = 3.0f;
        const auto leftHandle = juce::Rectangle<float> (
            viewport.getX() - handleWidth * 0.5f,
            overviewBounds.getY(), handleWidth, overviewBounds.getHeight());
        const auto rightHandle = juce::Rectangle<float> (
            viewport.getRight() - handleWidth * 0.5f,
            overviewBounds.getY(), handleWidth, overviewBounds.getHeight());
        g.setColour (juce::Colours::white.withAlpha (0.72f));
        g.fillRoundedRectangle (leftHandle, 1.0f);
        g.fillRoundedRectangle (rightHandle, 1.0f);
    }

    void mouseMove (const juce::MouseEvent& event) override
    {
        if (processor.getPadSample (padIndex) == nullptr)
        {
            setMouseCursor (juce::MouseCursor::NormalCursor);
            return;
        }

        if (getOverviewBounds().contains (event.position))
        {
            setMouseCursor (juce::MouseCursor::DraggingHandCursor);
            return;
        }

        if (getWaveformBounds().contains (event.position))
        {
            setMouseCursor (isNearMarker (event.position.x)
                                ? juce::MouseCursor::LeftRightResizeCursor
                                : juce::MouseCursor::PointingHandCursor);
            return;
        }

        setMouseCursor (juce::MouseCursor::NormalCursor);
    }

    void mouseDown (const juce::MouseEvent& event) override
    {
        if (processor.getPadSample (padIndex) == nullptr
            || ! event.mods.isLeftButtonDown())
            return;

        const auto overview = getOverviewBounds();

        if (overview.contains (event.position))
        {
            draggingMarker = false;
            draggingOverview = true;
            const auto overviewContent = getOverviewContentBounds();

            const float relativeMouse = juce::jlimit (
                0.0f, 1.0f,
                (event.position.x - overviewContent.getX())
                    / overviewContent.getWidth());
            const float viewLength = viewEnd - viewStart;

            if (getViewportBounds().contains (event.position))
            {
                overviewDragOffset = relativeMouse - viewStart;
            }
            else
            {
                overviewDragOffset = viewLength * 0.5f;
                moveViewToOverviewPosition (relativeMouse);
            }

            repaint();
            return;
        }

        const auto bounds = getWaveformBounds();

        if (! bounds.contains (event.position))
            return;

        if (event.getNumberOfClicks() > 1)
            return;

        const auto distances = getMarkerDistances (event.position.x);

        if (juce::jmin (distances.first, distances.second) <= markerGrabWidth)
        {
            draggingStart = distances.first <= distances.second;
            draggingMarker = true;
            updateMarker (event.position.x);
        }
        else
        {
            draggingMarker = false;
            processor.triggerPadFromInterface (padIndex, 1.0f);
        }
    }

    void mouseDrag (const juce::MouseEvent& event) override
    {
        if (draggingOverview)
        {
            const auto overview = getOverviewContentBounds();
            const float relativeMouse = juce::jlimit (
                0.0f, 1.0f,
                (event.position.x - overview.getX()) / overview.getWidth());
            moveViewToOverviewPosition (relativeMouse);
        }
        else if (draggingMarker)
            updateMarker (event.position.x);
    }

    void mouseUp (const juce::MouseEvent&) override
    {
        draggingMarker = false;
        draggingOverview = false;
    }

    void mouseWheelMove (const juce::MouseEvent& event,
                         const juce::MouseWheelDetails& wheel) override
    {
        const auto bounds = getWaveformBounds();

        if (processor.getPadSample (padIndex) == nullptr
            || wheel.deltaY == 0.0f
            || ! bounds.contains (event.position))
        {
            juce::Component::mouseWheelMove (event, wheel);
            return;
        }

        const float oldLength = viewEnd - viewStart;
        const float newLength = juce::jlimit (
            minimumViewLength, 1.0f,
            oldLength * (wheel.deltaY > 0.0f ? 0.80f : 1.25f));
        const float relativeMouse = juce::jlimit (
            0.0f, 1.0f,
            (event.position.x - bounds.getX()) / bounds.getWidth());
        const float anchor = viewStart + oldLength * relativeMouse;
        viewStart = juce::jlimit (
            0.0f, 1.0f - newLength, anchor - newLength * relativeMouse);
        viewEnd = viewStart + newLength;

        if (newLength > 0.995f)
            resetView();

        repaint();
    }

    void mouseDoubleClick (const juce::MouseEvent& event) override
    {
        if (! getWaveformBounds().contains (event.position)
            && ! getOverviewBounds().contains (event.position))
            return;

        processor.setPadSampleStart (padIndex, 0.0f);
        processor.setPadSampleEnd (padIndex, 1.0f);
        resetView();
        repaint();
    }

private:
    juce::Rectangle<float> getWaveformBounds() const
    {
        auto area = getLocalBounds().toFloat().reduced (8.0f);
        area.removeFromTop (20.0f);
        area.removeFromBottom (overviewHeight + overviewGap);
        return area;
    }

    juce::Rectangle<float> getOverviewBounds() const
    {
        auto area = getLocalBounds().toFloat().reduced (8.0f);
        area.removeFromTop (20.0f);
        return area.removeFromBottom (overviewHeight);
    }

    juce::Rectangle<float> getOverviewContentBounds() const
    {
        return getOverviewBounds().reduced (2.0f);
    }

    juce::Rectangle<float> getViewportBounds() const
    {
        const auto overview = getOverviewContentBounds();
        return { overview.getX() + overview.getWidth() * viewStart,
                 overview.getY(),
                 overview.getWidth() * (viewEnd - viewStart),
                 overview.getHeight() };
    }

    static void drawFilledWaveform (
        juce::Graphics& g,
        const SVDrummerAudioProcessor::SampleData& sample,
        juce::Rectangle<float> bounds,
        float normalisedStart,
        float normalisedEnd,
        juce::Colour colour,
        float fillAlpha,
        float lineAlpha)
    {
        if (sample.waveform.empty() || bounds.isEmpty())
            return;

        const int pointCount = static_cast<int> (sample.waveform.size());
        const int firstVisiblePoint = juce::jlimit (
            0, pointCount - 1,
            static_cast<int> (std::floor (
                normalisedStart * static_cast<float> (pointCount - 1))));
        const int lastVisiblePoint = juce::jlimit (
            firstVisiblePoint + 1, pointCount,
            static_cast<int> (std::ceil (
                normalisedEnd * static_cast<float> (pointCount))));
        const int visiblePointCount = lastVisiblePoint - firstVisiblePoint;
        const int columns = juce::jmax (
            2, juce::jmin (juce::roundToInt (bounds.getWidth()),
                           juce::jmax (2, visiblePointCount)));
        const float centreY = bounds.getCentreY();
        const float halfHeight = bounds.getHeight() * 0.46f;

        juce::Path positiveFill;
        juce::Path negativeFill;
        juce::Path positiveLine;
        juce::Path negativeLine;
        float finalX = bounds.getX();

        for (int column = 0; column < columns; ++column)
        {
            const int firstPoint = firstVisiblePoint
                                 + column * visiblePointCount / columns;
            const int lastPoint = juce::jmax (
                firstPoint + 1,
                firstVisiblePoint
                    + (column + 1) * visiblePointCount / columns);
            float minimum = 1.0f;
            float maximum = -1.0f;

            for (int point = firstPoint;
                 point < juce::jmin (lastVisiblePoint, lastPoint);
                 ++point)
            {
                const auto range = sample.waveform[static_cast<std::size_t> (point)];
                minimum = juce::jmin (minimum, range.getStart());
                maximum = juce::jmax (maximum, range.getEnd());
            }

            const float position = static_cast<float> (column)
                                 / static_cast<float> (columns - 1);
            const float x = bounds.getX() + bounds.getWidth() * position;
            const float positiveY = centreY
                                  - juce::jmax (0.0f, maximum) * halfHeight;
            const float negativeY = centreY
                                  - juce::jmin (0.0f, minimum) * halfHeight;

            if (column == 0)
            {
                positiveFill.startNewSubPath (x, centreY);
                positiveFill.lineTo (x, positiveY);
                negativeFill.startNewSubPath (x, centreY);
                negativeFill.lineTo (x, negativeY);
                positiveLine.startNewSubPath (x, positiveY);
                negativeLine.startNewSubPath (x, negativeY);
            }
            else
            {
                positiveFill.lineTo (x, positiveY);
                negativeFill.lineTo (x, negativeY);
                positiveLine.lineTo (x, positiveY);
                negativeLine.lineTo (x, negativeY);
            }

            finalX = x;
        }

        positiveFill.lineTo (finalX, centreY);
        positiveFill.closeSubPath();
        negativeFill.lineTo (finalX, centreY);
        negativeFill.closeSubPath();

        g.setColour (colour.withAlpha (fillAlpha));
        g.fillPath (positiveFill);
        g.fillPath (negativeFill);
        g.setColour (colour.withAlpha (lineAlpha));
        const juce::PathStrokeType stroke (1.0f);
        g.strokePath (positiveLine, stroke);
        g.strokePath (negativeLine, stroke);
    }

    void moveViewToOverviewPosition (float relativeMouse)
    {
        const float viewLength = viewEnd - viewStart;
        viewStart = juce::jlimit (
            0.0f, 1.0f - viewLength,
            relativeMouse - overviewDragOffset);
        viewEnd = viewStart + viewLength;
        repaint();
    }

    void updateMarker (float mouseX)
    {
        const auto bounds = getWaveformBounds();

        if (bounds.isEmpty())
            return;

        const float position = juce::jlimit (
            0.0f, 1.0f,
            viewStart + (viewEnd - viewStart)
                            * (mouseX - bounds.getX()) / bounds.getWidth());

        if (draggingStart)
            processor.setPadSampleStart (padIndex, position);
        else
            processor.setPadSampleEnd (padIndex, position);

        repaint();
    }

    std::pair<float, float> getMarkerDistances (float mouseX) const
    {
        const auto bounds = getWaveformBounds();
        const float startX = samplePositionToX (
            processor.getPadSampleStart (padIndex), bounds);
        const float endX = samplePositionToX (
            processor.getPadSampleEnd (padIndex), bounds);
        return { std::abs (mouseX - startX), std::abs (mouseX - endX) };
    }

    bool isNearMarker (float mouseX) const
    {
        const auto distances = getMarkerDistances (mouseX);
        return juce::jmin (distances.first, distances.second) <= markerGrabWidth;
    }

    float samplePositionToX (float samplePosition,
                             juce::Rectangle<float> bounds) const
    {
        return bounds.getX()
             + bounds.getWidth() * (samplePosition - viewStart)
                                   / juce::jmax (minimumViewLength,
                                                 viewEnd - viewStart);
    }

    void resetView()
    {
        viewStart = 0.0f;
        viewEnd = 1.0f;
    }

    SVDrummerAudioProcessor& processor;
    static constexpr float markerGrabWidth = 9.0f;
    static constexpr float minimumViewLength = 0.01f;
    static constexpr float overviewHeight = 28.0f;
    static constexpr float overviewGap = 5.0f;
    std::shared_ptr<const SVDrummerAudioProcessor::SampleData> displayedSample;
    int padIndex = 0;
    float viewStart = 0.0f;
    float viewEnd = 1.0f;
    float overviewDragOffset = 0.5f;
    bool draggingStart = true;
    bool draggingMarker = false;
    bool draggingOverview = false;
};

class SVDrummerPadSettingsPanel final : public juce::Component
{
public:
    explicit SVDrummerPadSettingsPanel (SVDrummerAudioProcessor& owner)
        : processor (owner), waveformEditor (owner)
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
        clearButton.setTooltip ("Clear the sample from this pad");
        clearButton.setColour (juce::TextButton::buttonColourId,
                               raisedPanelColour.darker (0.12f));
        clearButton.setColour (juce::TextButton::textColourOffId,
                               juce::Colour (0xffe99696));

        addAndMakeVisible (titleLabel);
        addAndMakeVisible (pathLabel);
        addAndMakeVisible (waveformEditor);
        addAndMakeVisible (reverseButton);
        addAndMakeVisible (clearButton);

        titleLabel.setColour (juce::Label::textColourId, textColour);
        titleLabel.setFont (juce::FontOptions (18.0f, juce::Font::bold));
        titleLabel.setMinimumHorizontalScale (0.72f);
        pathLabel.setColour (juce::Label::textColourId, mutedTextColour);
        pathLabel.setJustificationType (juce::Justification::centredRight);
        pathLabel.setFont (juce::FontOptions (10.5f));
        pathLabel.setMinimumHorizontalScale (0.55f);

        setPadIndex (0);
    }

    void setPadIndex (int newPadIndex)
    {
        padIndex = juce::jlimit (0, SVDrummerAudioProcessor::numberOfPads - 1,
                                newPadIndex);
        waveformEditor.setPadIndex (padIndex);
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

        titleLabel.setText ("PAD " + juce::String (padIndex + 1)
                                + "  —  " + processor.getPadDisplayName (padIndex),
                            juce::dontSendNotification);

        auto path = processor.getPadSamplePath (padIndex);
        pathLabel.setText (path.isNotEmpty() ? path : "No sample loaded",
                           juce::dontSendNotification);
        clearButton.setEnabled (path.isNotEmpty());
        waveformEditor.repaint();
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

        auto footer = getLocalBounds().reduced (14).removeFromBottom (24);
        g.setColour (mutedTextColour);
        g.setFont (10.5f);
        g.drawText ("Click the waveform to audition. Drag S/E to trim; double-click to reset.",
                    footer, juce::Justification::centredLeft, true);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (16);
        auto titleRow = area.removeFromTop (26);
        const int titleWidth = juce::jlimit (
            230, 360, static_cast<int> (titleRow.getWidth() * 0.45f));
        titleLabel.setBounds (titleRow.removeFromLeft (titleWidth));
        titleRow.removeFromLeft (8);
        clearButton.setBounds (titleRow.removeFromRight (24).reduced (1));
        titleRow.removeFromRight (5);
        pathLabel.setBounds (titleRow);
        area.removeFromTop (6);
        area.removeFromBottom (28);

        auto controls = area.removeFromBottom (66);
        area.removeFromBottom (6);
        waveformEditor.setBounds (area);

        layoutKnob (volumeLabel, volumeSlider, controls.removeFromLeft (58));
        controls.removeFromLeft (4);
        layoutKnob (panLabel, panSlider, controls.removeFromLeft (58));
        controls.removeFromLeft (4);
        layoutKnob (tuneLabel, tuneSlider, controls.removeFromLeft (58));
        controls.removeFromLeft (8);

        reverseButton.setBounds (controls.removeFromLeft (92).withSizeKeepingCentre (88, 24));
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
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 52, 14);
        slider.setRange (minimum, maximum, interval);
        slider.setMouseDragSensitivity (80);
        slider.setColour (juce::Slider::rotarySliderFillColourId, juce::Colour (0xff5fa3d1));
        slider.setColour (juce::Slider::textBoxTextColourId, textColour);
        slider.setColour (juce::Slider::textBoxBackgroundColourId,
                          juce::Colours::transparentBlack);
        slider.setColour (juce::Slider::textBoxOutlineColourId,
                          juce::Colours::transparentBlack);

        label.setText (labelText, juce::dontSendNotification);
        label.setJustificationType (juce::Justification::centredTop);
        label.setColour (juce::Label::textColourId, textColour);
        label.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        addAndMakeVisible (slider);
        addAndMakeVisible (label);
    }

    static void layoutKnob (juce::Label& label,
                            juce::Slider& slider,
                            juce::Rectangle<int> bounds)
    {
        label.setBounds (bounds.removeFromTop (13));
        slider.setBounds (bounds);
    }

    SVDrummerAudioProcessor& processor;
    SVDrummerWaveformEditor waveformEditor;
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
    juce::TextButton clearButton { "X" };
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
        const float diameter = static_cast<float> (
            juce::jmin (getWidth(), getHeight())) - 1.0f;
        auto bounds = juce::Rectangle<float> (diameter, diameter)
                          .withCentre (getLocalBounds().toFloat().getCentre());
        auto background = getToggleState() ? juce::Colour (0xff3e536a)
                                            : raisedPanelColour;

        if (down)
            background = background.brighter (0.18f);
        else if (highlighted)
            background = background.brighter (0.09f);

        g.setColour (background);
        g.fillEllipse (bounds);
        g.setColour (lineColour.brighter (highlighted ? 0.22f : 0.0f));
        g.drawEllipse (bounds.reduced (0.5f), 1.2f);

        const auto icon = bounds.reduced (bounds.getWidth() * 0.31f);
        g.setColour (textColour);

        if (getToggleState())
        {
            const float side = juce::jmin (icon.getWidth(), icon.getHeight());
            g.fillRoundedRectangle (
                juce::Rectangle<float> (side, side).withCentre (icon.getCentre()), 1.0f);
        }
        else
        {
            const auto centre = icon.getCentre();
            const float iconSize = juce::jmin (icon.getWidth(), icon.getHeight());
            juce::Path triangle;
            triangle.addTriangle (
                centre.x - iconSize / 3.0f, centre.y - iconSize * 0.5f,
                centre.x - iconSize / 3.0f, centre.y + iconSize * 0.5f,
                centre.x + iconSize * 2.0f / 3.0f, centre.y);
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
        setTooltip ("Click to store the current pattern and load this slot; "
                    "drag a .svpattern here to assign it; mouse-wheel changes "
                    "its MIDI note; right-click for save and MIDI options");
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
        g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
        g.drawText (juce::String (patternIndex + 1),
                    numberArea, juce::Justification::centred);

        const int midiNote = processor.getPatternMidiNote (patternIndex);
        g.setColour (midiNote >= 0 ? textColour.withAlpha (0.90f)
                                   : mutedTextColour.withAlpha (0.88f));
        g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
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
            if (processor.isPatternMidiGateMode())
            {
                syncFromProcessor();
                return;
            }

            processor.setSequencerEnabled (enableButton.getToggleState());
            syncFromProcessor();
        };

        configureLabel (midiModeLabel, "MIDI MODE");
        midiModeButton.setClickingTogglesState (true);
        midiModeButton.setTooltip (
            "SELECT changes patterns only; GATE starts on note-on and stops on note-off");
        midiModeButton.setColour (juce::TextButton::buttonColourId,
                                  raisedPanelColour);
        midiModeButton.setColour (juce::TextButton::buttonOnColourId,
                                  juce::Colour (0xff3e536a));
        midiModeButton.onClick = [this]
        {
            processor.setPatternMidiGateMode (
                midiModeButton.getToggleState());
            syncFromProcessor();
        };

        configureSequencerKnob (lengthSlider, 1.0,
                                static_cast<double> (SVDrummerAudioProcessor::maximumPatternBars));
        lengthSlider.textFromValueFunction = [] (double value)
        {
            const int bars = juce::roundToInt (value);
            return juce::String (bars) + (bars == 1 ? " Bar" : " Bars");
        };
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
            const int bars = index == 0 ? 1 : index == 1 ? 2 : 4;
            return juce::String (bars) + (bars == 1 ? " Bar" : " Bars");
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
        loopLengthSlider.textFromValueFunction = [] (double value)
        {
            const int steps = juce::roundToInt (value);
            return juce::String (steps) + (steps == 1 ? " Step" : " Steps");
        };
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
        addAndMakeVisible (midiModeLabel);
        addAndMakeVisible (midiModeButton);
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
        viewSlider.updateText();
        updatingControls = false;
        syncFromProcessor();
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

        if (! rulerBounds.isEmpty())
        {
            auto rulerSteps = rulerBounds;
            auto rulerLabel = rulerSteps.removeFromLeft (laneLabelWidth);
            rulerSteps.removeFromLeft (4);
            const int startBar = getStartBar();
            const int barsShown = getBarsShown();
            const float barWidth = static_cast<float> (rulerSteps.getWidth())
                                 / static_cast<float> (juce::jmax (1, barsShown));

            g.setColour (mutedTextColour.brighter (0.12f));
            g.setFont (juce::FontOptions (10.5f, juce::Font::bold));
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

        drawBeatGrid (g);
        drawSequenceRows (g);
        drawPlayhead (g);

        auto patternArea = patternBounds;
        auto patternLabel = patternArea.removeFromLeft (laneLabelWidth);
        g.setColour (mutedTextColour);
        g.setFont (juce::FontOptions (9.5f, juce::Font::bold));
        g.drawText ("PATTERN", patternLabel, juce::Justification::centredLeft);
    }

    void resized() override
    {
        auto area = getLocalBounds().reduced (10);

        auto controls = area.removeFromTop (66);
        constexpr int transportSize = 40;
        const int controlY = controls.getY()
                           + (controls.getHeight() - transportSize) / 2;
        enableButton.setBounds (
            controls.removeFromLeft (44).withY (controlY).withHeight (transportSize)
                    .withSizeKeepingCentre (transportSize, transportSize));
        controls.removeFromLeft (8);

        auto midiModeGroup = controls.removeFromLeft (74);
        midiModeLabel.setBounds (midiModeGroup.removeFromTop (13));
        midiModeButton.setBounds (
            midiModeGroup.withY (controlY + 6).withHeight (27).reduced (3, 0));
        controls.removeFromLeft (6);

        auto placeKnob = [&controls] (juce::Label& label, juce::Slider& slider)
        {
            auto group = controls.removeFromLeft (66);
            label.setBounds (group.removeFromTop (13));
            slider.setBounds (group);
            controls.removeFromLeft (4);
        };

        placeKnob (lengthLabel, lengthSlider);
        placeKnob (viewLabel, viewSlider);
        laneButton.setBounds (controls.removeFromLeft (72)
                                      .withY (controlY + 1).withHeight (27));
        controls.removeFromLeft (8);
        placeKnob (divisionLabel, divisionSlider);
        placeKnob (loopLabel, loopLengthSlider);

        area.removeFromTop (1);
        patternBounds = area.removeFromBottom (30);
        area.removeFromBottom (4);
        barScroll.setBounds (area.removeFromBottom (12));
        area.removeFromBottom (3);
        rulerBounds = area.removeFromTop (17);
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

        if (! event.mods.isLeftButtonDown() && ! event.mods.isRightButtonDown())
            return;

        gestureLane = lane;
        gestureVelocity = event.mods.isRightButtonDown()
                            ? 0
                            : lastDrawVelocity;
        lastGestureVisibleStep = visibleStep;
        stepGestureActive = true;
        applyStepGesture (gestureLane, visibleStep);
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
            applyStepGesture (gestureLane, step);

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
        slider.setTextBoxStyle (juce::Slider::TextBoxBelow, true, 64, 14);
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
        const bool gateMode = processor.isPatternMidiGateMode();
        enableButton.setToggleState (enabled, juce::dontSendNotification);
        enableButton.setTooltip (
            gateMode
                ? "Pattern-note gate status: note-on starts and note-off stops"
                : "Start or stop the sequencer while the host transport is running");
        enableButton.repaint();
        midiModeButton.setToggleState (gateMode, juce::dontSendNotification);
        midiModeButton.setButtonText (gateMode ? "GATE" : "SELECT");
        lengthSlider.setValue (processor.getPatternBars(), juce::dontSendNotification);
        lengthSlider.updateText();
        viewSlider.updateText();
        updatingControls = false;
        updateScrollRange();
        syncLaneControls();
    }

    void syncLaneControls()
    {
        updatingControls = true;
        const int division = processor.getLaneDivision (selectedLane);
        divisionSlider.setValue (division, juce::dontSendNotification);
        divisionSlider.updateText();
        loopLengthSlider.setRange (1.0,
                                   static_cast<double> (
                                       processor.getLaneMaximumLoopLength (selectedLane)),
                                   1.0);
        loopLengthSlider.setValue (processor.getLaneLoopLength (selectedLane),
                                   juce::dontSendNotification);
        loopLengthSlider.updateText();
        laneButton.setButtonText ("LANE " + juce::String (selectedLane + 1));
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

    void applyStepGesture (int lane, int visibleStep)
    {
        const int dataStep = dataStepForVisibleStep (lane, visibleStep);
        const int currentVelocity = processor.getSequenceStepVelocity (lane, dataStep);

        if (gestureVelocity == 0 || currentVelocity <= 0)
            processor.setSequenceStepVelocity (lane, dataStep, gestureVelocity);

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
            g.setFont (juce::FontOptions (
                juce::jmax (9.0f, juce::jmin (10.5f, rowHeight * 0.62f)),
                juce::Font::bold));
            g.drawFittedText (
                juce::String (lane + 1) + "  "
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

                if (dataStep == 0 && globalStep > 0
                    && visibleStep % stepsPerBar != 0)
                {
                    g.setColour (accent.withAlpha (repeatedOccurrence ? 0.26f : 0.50f));
                    g.drawVerticalLine (juce::roundToInt (cell.getX()),
                                        row.getY(), row.getBottom());
                }
            }
        }
    }

    void drawBeatGrid (juce::Graphics& g)
    {
        if (sequenceRowsBounds.isEmpty())
            return;

        auto stepArea = sequenceRowsBounds.toFloat();
        stepArea.removeFromLeft (static_cast<float> (laneLabelWidth + 4));
        const int totalBeats = juce::jmax (1, getBarsShown() * 4);

        for (int beat = 0; beat <= totalBeats; ++beat)
        {
            const bool barLine = beat % 4 == 0;
            const float x = stepArea.getX()
                          + stepArea.getWidth() * static_cast<float> (beat)
                                                   / static_cast<float> (totalBeats);
            g.setColour (barLine ? textColour.withAlpha (0.46f)
                                 : lineColour.brighter (0.32f));
            g.drawLine (x, stepArea.getY(), x, stepArea.getBottom(),
                        barLine ? 1.8f : 1.0f);
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
    juce::Label midiModeLabel;
    juce::TextButton midiModeButton { "SELECT" };
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
    int laneLabelWidth = 74;
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
    sequencerViewButton.setClickingTogglesState (false);
    settingsViewButton.setClickingTogglesState (false);
    sequencerViewButton.setConnectedEdges (juce::Button::ConnectedOnRight
                                           | juce::Button::ConnectedOnBottom);
    settingsViewButton.setConnectedEdges (juce::Button::ConnectedOnLeft
                                          | juce::Button::ConnectedOnBottom);

    for (auto* tab : { &sequencerViewButton, &settingsViewButton })
    {
        tab->setColour (juce::TextButton::buttonColourId,
                        raisedPanelColour.darker (0.42f));
        tab->setColour (juce::TextButton::buttonOnColourId, panelColour);
        tab->setColour (juce::TextButton::textColourOffId,
                        mutedTextColour.darker (0.12f));
        tab->setColour (juce::TextButton::textColourOnId, textColour);
    }

    addAndMakeVisible (sequencerViewButton);
    addAndMakeVisible (settingsViewButton);

    showSequencerView();

    setResizable (true, true);
    setResizeLimits (baseEditorWidth * 3 / 4, baseEditorHeight * 3 / 4,
                     baseEditorWidth * 2, baseEditorHeight * 2);
    getConstrainer()->setFixedAspectRatio (
        static_cast<double> (baseEditorWidth) / static_cast<double> (baseEditorHeight));
    initialLayoutComplete = true;
    const int savedZoom = loadSavedZoomPercent();
    setSize (juce::roundToInt (static_cast<double> (baseEditorWidth)
                               * static_cast<double> (savedZoom) / 100.0),
             juce::roundToInt (static_cast<double> (baseEditorHeight)
                               * static_cast<double> (savedZoom) / 100.0));
    zoomSavePending = false;
    zoomSaveTimerTicks = 0;
    startTimerHz (20);
}

SVDrummerAudioProcessorEditor::~SVDrummerAudioProcessorEditor()
{
    stopTimer();
    saveZoomSetting();
    processor.flushPortableSettingsIfNeeded();
    setLookAndFeel (nullptr);
}

void SVDrummerAudioProcessorEditor::paint (juce::Graphics& g)
{
    g.fillAll (backgroundColour);
    g.saveState();
    g.addTransform (juce::AffineTransform (
        uiScale, 0.0f, uiOffsetX,
        0.0f, uiScale, uiOffsetY));

    auto header = juce::Rectangle<int> (0, 0, baseEditorWidth, baseEditorHeight)
                      .reduced (10).removeFromTop (42);
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
    g.drawFittedText ("16-PAD SAMPLE PLAYER  -  STAGE 4.6",
                      header, juce::Justification::centredRight, 1);
    g.restoreState();
}

void SVDrummerAudioProcessorEditor::resized()
{
    uiScale = juce::jmax (0.01f, juce::jmin (
        static_cast<float> (getWidth()) / static_cast<float> (baseEditorWidth),
        static_cast<float> (getHeight()) / static_cast<float> (baseEditorHeight)));
    uiOffsetX = (static_cast<float> (getWidth())
                 - static_cast<float> (baseEditorWidth) * uiScale) * 0.5f;
    uiOffsetY = (static_cast<float> (getHeight())
                 - static_cast<float> (baseEditorHeight) * uiScale) * 0.5f;

    auto area = juce::Rectangle<int> (0, 0, baseEditorWidth, baseEditorHeight).reduced (10);
    area.removeFromTop (48);

    const int browserWidth = juce::jlimit (245, 390,
                                           static_cast<int> (baseEditorWidth * 0.225f));
    browserPanel->setBounds (area.removeFromLeft (browserWidth));
    area.removeFromLeft (10);

    const int gap = juce::jlimit (3, 7, baseEditorWidth / 210);
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
    auto viewButtons = area.removeFromTop (26);
    sequencerViewButton.setBounds (viewButtons.removeFromLeft (132).withHeight (28));
    settingsViewButton.setBounds (viewButtons.removeFromLeft (132).withHeight (28));

    sequencerPanel->setBounds (area);
    padSettingsPanel->setBounds (area);

    const auto transform = juce::AffineTransform (
        uiScale, 0.0f, uiOffsetX,
        0.0f, uiScale, uiOffsetY);
    browserPanel->setTransform (transform);

    for (auto& pad : padComponents)
        pad->setTransform (transform);

    sequencerViewButton.setTransform (transform);
    settingsViewButton.setTransform (transform);
    sequencerPanel->setTransform (transform);
    padSettingsPanel->setTransform (transform);

    if (initialLayoutComplete)
    {
        zoomSavePending = true;
        zoomSaveTimerTicks = 0;
    }
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

    if (zoomSavePending && ++zoomSaveTimerTicks >= 10)
    {
        saveZoomSetting();
        zoomSavePending = false;
        zoomSaveTimerTicks = 0;
    }
}

int SVDrummerAudioProcessorEditor::loadSavedZoomPercent() const
{
    const auto settingsFile = processor.getPortableDataDirectory()
                                       .getChildFile ("Settings.ini");

    if (! settingsFile.existsAsFile())
        return 100;

    juce::StringArray lines;
    lines.addLines (settingsFile.loadFileAsString());

    for (auto line : lines)
    {
        line = line.trim();

        if (line.startsWithIgnoreCase ("ZoomPercent="))
            return juce::jlimit (75, 200,
                                 line.fromFirstOccurrenceOf ("=", false, false)
                                     .getIntValue());
    }

    return 100;
}

void SVDrummerAudioProcessorEditor::saveZoomSetting() const
{
    if (getWidth() <= 0)
        return;

    const auto settingsFile = processor.getPortableDataDirectory()
                                       .getChildFile ("Settings.ini");
    settingsFile.getParentDirectory().createDirectory();
    const int zoomPercent = juce::jlimit (
        75, 200, juce::roundToInt (static_cast<double> (uiScale) * 100.0));
    settingsFile.replaceWithText (
        "; SV-Drummer GUI settings\r\n"
        "[GUI]\r\n"
        "ZoomPercent=" + juce::String (zoomPercent) + "\r\n",
        false, false);
}

void SVDrummerAudioProcessorEditor::showSequencerView()
{
    showingSettings = false;

    for (auto& pad : padComponents)
        pad->setEditingSelected (false);

    sequencerPanel->setVisible (true);
    padSettingsPanel->setVisible (false);
    updateViewButtons();
}

void SVDrummerAudioProcessorEditor::showPadSettings (int padIndex)
{
    selectedPad = juce::jlimit (0, SVDrummerAudioProcessor::numberOfPads - 1,
                               padIndex);
    showingSettings = true;

    for (int index = 0; index < SVDrummerAudioProcessor::numberOfPads; ++index)
        padComponents[static_cast<std::size_t> (index)]->setEditingSelected (
            index == selectedPad);

    padSettingsPanel->setPadIndex (selectedPad);
    sequencerPanel->setVisible (false);
    padSettingsPanel->setVisible (true);
    updateViewButtons();
}

void SVDrummerAudioProcessorEditor::updateViewButtons()
{
    sequencerViewButton.setToggleState (! showingSettings, juce::dontSendNotification);
    settingsViewButton.setToggleState (showingSettings, juce::dontSendNotification);
    sequencerViewButton.repaint();
    settingsViewButton.repaint();
    repaint();
}
