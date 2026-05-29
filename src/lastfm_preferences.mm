//
//  lastfm_preferences.mm
//  foo_scrobbler_mac
//
//  (c) 2025-2026 by Konstantinos Kyriakopoulos
//

#import "stdafx.h"

#include "debug.h"
#include "lastfm_settings.h"

#include <cctype>
#include <string>
#include <vector>

namespace
{
static const GUID GUID_LASTFM_PREFERENCES_PAGE = {
    0x7ef08a13, 0xe096, 0x4ef2, {0xa1, 0x4b, 0xec, 0x7c, 0xd0, 0xd6, 0x46, 0x35}};

static NSString* nsString(const std::string& value)
{
    return [NSString stringWithUTF8String:value.c_str()];
}

static std::string cppString(NSString* value)
{
    const char* utf8 = value.UTF8String;
    return utf8 ? std::string(utf8) : std::string();
}

static NSTextField* makeLabel(NSString* text)
{
    NSTextField* label = [NSTextField labelWithString:text];
    label.alignment = NSTextAlignmentRight;
    label.translatesAutoresizingMaskIntoConstraints = NO;
    [label.widthAnchor constraintEqualToConstant:190.0].active = YES;
    return label;
}

static NSTextField* makeHeader(NSString* text)
{
    NSTextField* label = [NSTextField labelWithString:text];
    label.font = [NSFont boldSystemFontOfSize:[NSFont systemFontSize]];
    label.translatesAutoresizingMaskIntoConstraints = NO;
    return label;
}

static NSStackView* makeRowWithLabel(NSTextField* label, NSView* control)
{
    NSStackView* row = [NSStackView stackViewWithViews:@[ label, control ]];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.alignment = NSLayoutAttributeCenterY;
    row.spacing = 10.0;
    row.translatesAutoresizingMaskIntoConstraints = NO;
    return row;
}

static NSStackView* makeRow(NSString* labelText, NSView* control)
{
    return makeRowWithLabel(makeLabel(labelText), control);
}

static NSStackView* makeControlRow(NSView* control)
{
    NSTextField* spacer = makeLabel(@"");
    NSStackView* row = [NSStackView stackViewWithViews:@[ spacer, control ]];
    row.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    row.alignment = NSLayoutAttributeCenterY;
    row.spacing = 10.0;
    row.translatesAutoresizingMaskIntoConstraints = NO;
    return row;
}

static NSStackView* makeStack()
{
    NSStackView* stack = [[NSStackView alloc] initWithFrame:NSZeroRect];
    stack.orientation = NSUserInterfaceLayoutOrientationVertical;
    stack.alignment = NSLayoutAttributeLeading;
    stack.spacing = 12.0;
    stack.translatesAutoresizingMaskIntoConstraints = NO;
    return stack;
}

static NSView* makeWrappedStack(NSStackView* stack)
{
    NSView* wrapper = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 620, 360)];
    [wrapper addSubview:stack];

    [NSLayoutConstraint activateConstraints:@[
        [stack.leadingAnchor constraintEqualToAnchor:wrapper.leadingAnchor constant:20.0],
        [stack.trailingAnchor constraintLessThanOrEqualToAnchor:wrapper.trailingAnchor constant:-20.0],
        [stack.topAnchor constraintEqualToAnchor:wrapper.topAnchor constant:20.0],
    ]];

    return wrapper;
}

enum TextFieldTag
{
    TextFieldArtistTf = 1,
    TextFieldAlbumArtistTf,
    TextFieldTitleTf,
    TextFieldAlbumTf,
    TextFieldExcludeArtists,
    TextFieldExcludeTitles,
    TextFieldExcludeAlbums,
    TextFieldExcludeTf,
};

enum ExclusionTemplateTag
{
    ExclusionTemplateGenre = 101,
    ExclusionTemplateMediaKind,
    ExclusionTemplatePath,
    ExclusionTemplateComment,
};

struct TextFieldSetting
{
    NSInteger tag;
    NSString* label;
    std::string (*getValue)();
    void (*setValue)(const std::string&);
};

static const TextFieldSetting kTextFields[] = {
    {TextFieldArtistTf, @"Artist:", lastfm::settings::artistTitleFormat, lastfm::settings::setArtistTitleFormat},
    {TextFieldAlbumArtistTf, @"Album artist:", lastfm::settings::albumArtistTitleFormat, lastfm::settings::setAlbumArtistTitleFormat},
    {TextFieldTitleTf, @"Title:", lastfm::settings::titleTitleFormat, lastfm::settings::setTitleTitleFormat},
    {TextFieldAlbumTf, @"Album:", lastfm::settings::albumTitleFormat, lastfm::settings::setAlbumTitleFormat},
    {TextFieldExcludeArtists, @"Artists:", lastfm::settings::excludedArtistsPatternList, lastfm::settings::setExcludedArtistsPatternList},
    {TextFieldExcludeTitles, @"Titles:", lastfm::settings::excludedTitlesPatternList, lastfm::settings::setExcludedTitlesPatternList},
    {TextFieldExcludeAlbums, @"Albums:", lastfm::settings::excludedAlbumsPatternList, lastfm::settings::setExcludedAlbumsPatternList},
    {TextFieldExcludeTf, @"Title Formatting:", lastfm::settings::excludedTitleFormatExpression, lastfm::settings::setExcludedTitleFormatExpression},
};

struct ExclusionTemplate
{
    NSInteger tag;
    NSString* label;
    const char* field;
    bool contains;
    std::string (*getValue)();
    void (*setValue)(const std::string&);
};

static const ExclusionTemplate kTemplates[] = {
    {ExclusionTemplateGenre, @"Genre is:", "%Genre%", false, lastfm::settings::excludedGenreTemplateValueList,
     lastfm::settings::setExcludedGenreTemplateValueList},
    {ExclusionTemplateMediaKind, @"Media kind is:", "%Media Kind%", false,
     lastfm::settings::excludedMediaKindTemplateValueList, lastfm::settings::setExcludedMediaKindTemplateValueList},
    {ExclusionTemplatePath, @"Path contains:", "$if2(%Path%,) $if2(%FOO_SCROBBLER_PATH%,)", true, lastfm::settings::excludedPathTemplateValueList,
     lastfm::settings::setExcludedPathTemplateValueList},
    {ExclusionTemplateComment, @"Comment contains:", "%Comment%", true,
     lastfm::settings::excludedCommentTemplateValueList, lastfm::settings::setExcludedCommentTemplateValueList},
};

static const ExclusionTemplate* findTemplate(NSInteger tag)
{
    for (const auto& t : kTemplates)
        if (t.tag == tag)
            return &t;
    return nullptr;
}

static std::string trimCopy(const std::string& in)
{
    std::size_t b = 0;
    while (b < in.size() && std::isspace((unsigned char)in[b]))
        ++b;

    std::size_t e = in.size();
    while (e > b && std::isspace((unsigned char)in[e - 1]))
        --e;

    return (e > b) ? in.substr(b, e - b) : std::string{};
}

static std::string lowerCopy(const std::string& in)
{
    std::string out;
    out.reserve(in.size());
    for (unsigned char c : in)
        out.push_back((char)std::tolower(c));
    return out;
}

static std::string titleFormatLiteral(const std::string& value)
{
    std::string out = "'";
    for (char c : value)
    {
        if (c == '\'')
            out += "''";
        else
            out.push_back(c);
    }
    out += "'";
    return out;
}

static std::string makeTemplateExpression(const ExclusionTemplate& t, const std::string& rawValues)
{
    std::vector<std::string> conditions;
    std::size_t start = 0;
    while (start <= rawValues.size())
    {
        std::size_t end = rawValues.find(';', start);
        if (end == std::string::npos)
            end = rawValues.size();

        const std::string value = trimCopy(rawValues.substr(start, end - start));
        if (!value.empty())
            conditions.push_back(t.contains ? "$strstr($lower(" + std::string(t.field) + ")," +
                                              titleFormatLiteral(lowerCopy(value)) + ")"
                                            : "$stricmp(" + std::string(t.field) + "," +
                                              titleFormatLiteral(value) + ")");

        start = end + 1;
    }

    if (conditions.empty())
        return {};

    std::string predicate = conditions.front();
    if (conditions.size() > 1)
    {
        predicate = "$or(";
        for (std::size_t i = 0; i < conditions.size(); ++i)
            predicate += (i ? "," : "") + conditions[i];
        predicate += ")";
    }

    return "$if(" + predicate + ",1,)";
}

static std::string removeTemplateExpr(std::string text, const std::string& expr)
{
    if (expr.empty())
        return text;

    for (std::size_t pos = text.find(expr); pos != std::string::npos; pos = text.find(expr, pos))
    {
        text.erase(pos, expr.size());
        if (pos < text.size() && text[pos] == ' ')
            text.erase(pos, 1);
        else if (pos > 0 && text[pos - 1] == ' ')
            text.erase(pos - 1, 1);
    }
    return trimCopy(text);
}

static bool hasTemplateExpr(const std::string& text, const std::string& expr)
{
    return !expr.empty() && text.find(expr) != std::string::npos;
}

static std::string appendTemplateExpr(std::string text, const std::string& expr)
{
    if (expr.empty() || hasTemplateExpr(text, expr))
        return text;
    text = trimCopy(text);
    text += text.empty() ? expr : " " + expr;
    return text;
}
} // namespace

@interface LastfmPreferencesController : NSViewController <NSTextFieldDelegate>
@property(nonatomic, strong) NSPopUpButton* consolePopup;
@property(nonatomic, strong) NSButton* disableNowPlayingCheckbox;
@property(nonatomic, strong) NSButton* onlyLibraryCheckbox;
@property(nonatomic, strong) NSPopUpButton* dynamicPopup;
@property(nonatomic, strong) NSTextField* dynamicSourcesLabel;
@property(nonatomic, strong) NSButton* treatVariousArtistsCheckbox;
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, NSTextField*>* textFields;
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, NSButton*>* templateCheckboxes;
@property(nonatomic, strong) NSMutableDictionary<NSNumber*, NSTextField*>* templateFields;
@end

@implementation LastfmPreferencesController

- (void)loadView
{
    self.view = [[NSView alloc] initWithFrame:NSMakeRect(0, 0, 660, 430)];
    self.textFields = [NSMutableDictionary dictionary];

    NSTabView* tabs = [[NSTabView alloc] initWithFrame:NSInsetRect(self.view.bounds, 16.0, 16.0)];
    tabs.autoresizingMask = NSViewWidthSizable | NSViewHeightSizable;
    [self.view addSubview:tabs];

    [tabs addTabViewItem:[self tabWithIdentifier:@"console" label:@"Console" view:[self makeConsoleView]]];
    [tabs addTabViewItem:[self tabWithIdentifier:@"scrobbling" label:@"Scrobbling" view:[self makeScrobblingView]]];
    [tabs addTabViewItem:[self tabWithIdentifier:@"tags" label:@"Tags" view:[self makeTagsView]]];
    [tabs addTabViewItem:[self tabWithIdentifier:@"exclusions" label:@"Exclusions" view:[self makeExclusionsView]]];

    [self loadSettings];
}

- (NSTabViewItem*)tabWithIdentifier:(NSString*)identifier label:(NSString*)label view:(NSView*)view
{
    NSTabViewItem* item = [[NSTabViewItem alloc] initWithIdentifier:identifier];
    item.label = label;
    item.view = view;
    return item;
}

- (NSPopUpButton*)newPopupWithItems:(NSArray<NSString*>*)items action:(SEL)action
{
    NSPopUpButton* popup = [[NSPopUpButton alloc] initWithFrame:NSZeroRect pullsDown:NO];
    [popup addItemsWithTitles:items];
    popup.target = self;
    popup.action = action;
    popup.translatesAutoresizingMaskIntoConstraints = NO;
    [popup.widthAnchor constraintGreaterThanOrEqualToConstant:260.0].active = YES;
    return popup;
}

- (NSButton*)newCheckboxWithTitle:(NSString*)title action:(SEL)action
{
    NSButton* button = [NSButton checkboxWithTitle:title target:self action:action];
    button.translatesAutoresizingMaskIntoConstraints = NO;
    return button;
}

- (NSTextField*)newTextFieldWithTag:(NSInteger)tag
{
    NSTextField* field = [NSTextField textFieldWithString:@""];
    field.delegate = self;
    field.target = self;
    field.action = @selector(onTextField:);
    field.tag = tag;
    field.usesSingleLineMode = YES;
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    [field.widthAnchor constraintGreaterThanOrEqualToConstant:360.0].active = YES;
    return field;
}

- (void)addTextFieldsFrom:(NSInteger)firstTag through:(NSInteger)lastTag toStack:(NSStackView*)stack
{
    for (const auto& setting : kTextFields)
    {
        if (setting.tag < firstTag || setting.tag > lastTag)
            continue;

        NSTextField* field = [self newTextFieldWithTag:setting.tag];
        self.textFields[@(setting.tag)] = field;
        [stack addArrangedSubview:makeRow(setting.label, field)];
    }
}

- (NSButton*)newTemplateCheckboxWithTitle:(NSString*)title tag:(NSInteger)tag
{
    NSButton* button = [self newCheckboxWithTitle:title action:@selector(onTemplateCheckbox:)];
    button.tag = tag;
    return button;
}

- (NSTextField*)newTemplateTextFieldWithTag:(NSInteger)tag
{
    NSTextField* field = [NSTextField textFieldWithString:@""];
    field.delegate = self;
    field.target = self;
    field.action = @selector(onTemplateTextField:);
    field.tag = tag;
    field.usesSingleLineMode = YES;
    field.lineBreakMode = NSLineBreakByTruncatingTail;
    field.translatesAutoresizingMaskIntoConstraints = NO;
    [field.widthAnchor constraintGreaterThanOrEqualToConstant:260.0].active = YES;
    return field;
}

- (NSStackView*)makeTemplateRowWithCheckbox:(NSButton*)checkbox field:(NSTextField*)field
{
    [checkbox.widthAnchor constraintGreaterThanOrEqualToConstant:145.0].active = YES;

    NSStackView* controls = [NSStackView stackViewWithViews:@[ checkbox, field ]];
    controls.orientation = NSUserInterfaceLayoutOrientationHorizontal;
    controls.alignment = NSLayoutAttributeCenterY;
    controls.spacing = 8.0;
    controls.translatesAutoresizingMaskIntoConstraints = NO;
    return makeControlRow(controls);
}

- (NSView*)makeConsoleView
{
    NSStackView* stack = makeStack();
    [stack addArrangedSubview:makeHeader(@"Console")];

    self.consolePopup = [self newPopupWithItems:@[ @"None", @"Basic", @"Debug" ] action:@selector(onConsolePopup:)];
    [stack addArrangedSubview:makeRow(@"Console info:", self.consolePopup)];

    return makeWrappedStack(stack);
}

- (NSView*)makeScrobblingView
{
    NSStackView* stack = makeStack();
    [stack addArrangedSubview:makeHeader(@"Scrobbling")];

    self.disableNowPlayingCheckbox =
        [self newCheckboxWithTitle:@"Disable Now Playing notifications" action:@selector(onDisableNowPlaying:)];
    self.onlyLibraryCheckbox =
        [self newCheckboxWithTitle:@"Only scrobble from media library" action:@selector(onOnlyLibrary:)];

    [stack addArrangedSubview:makeControlRow(self.disableNowPlayingCheckbox)];
    [stack addArrangedSubview:makeControlRow(self.onlyLibraryCheckbox)];

    [stack addArrangedSubview:makeHeader(@"Dynamic Sources")];

    self.dynamicPopup = [self newPopupWithItems:@[ @"No dynamic sources", @"Only Now Playing", @"Now Playing and scrobbling" ]
                                         action:@selector(onDynamicPopup:)];
    self.dynamicSourcesLabel = makeLabel(@"Use:");
    [stack addArrangedSubview:makeRowWithLabel(self.dynamicSourcesLabel, self.dynamicPopup)];

    return makeWrappedStack(stack);
}

- (NSView*)makeTagsView
{
    NSStackView* stack = makeStack();
    [stack addArrangedSubview:makeHeader(@"Tag Formatting")];

    [self addTextFieldsFrom:TextFieldArtistTf through:TextFieldAlbumTf toStack:stack];

    self.treatVariousArtistsCheckbox =
        [self newCheckboxWithTitle:@"Treat \"Various Artists\" as empty for album artist"
                            action:@selector(onTreatVariousArtists:)];
    [stack addArrangedSubview:makeControlRow(self.treatVariousArtistsCheckbox)];

    return makeWrappedStack(stack);
}

- (NSView*)makeExclusionsView
{
    NSStackView* stack = makeStack();
    self.templateCheckboxes = [NSMutableDictionary dictionary];
    self.templateFields = [NSMutableDictionary dictionary];

    [stack addArrangedSubview:makeHeader(@"Text or Regex")];

    [self addTextFieldsFrom:TextFieldExcludeArtists through:TextFieldExcludeTf toStack:stack];

    [stack addArrangedSubview:makeHeader(@"TF Templates")];

    for (const auto& t : kTemplates)
    {
        NSNumber* key = @(t.tag);
        NSButton* checkbox = [self newTemplateCheckboxWithTitle:t.label tag:t.tag];
        NSTextField* field = [self newTemplateTextFieldWithTag:t.tag];
        self.templateCheckboxes[key] = checkbox;
        self.templateFields[key] = field;
        [stack addArrangedSubview:[self makeTemplateRowWithCheckbox:checkbox field:field]];
    }

    return makeWrappedStack(stack);
}

- (void)refreshTemplateCheckboxStates
{
    const std::string tf = cppString(self.textFields[@(TextFieldExcludeTf)].stringValue);
    for (const auto& t : kTemplates)
        self.templateCheckboxes[@(t.tag)].state =
            hasTemplateExpr(tf, makeTemplateExpression(t, t.getValue())) ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)applyTemplate:(const ExclusionTemplate&)t enabled:(bool)enabled value:(NSString*)value
{
    const std::string oldExpr = makeTemplateExpression(t, t.getValue());
    const std::string newValue = cppString(value);
    const std::string newExpr = makeTemplateExpression(t, newValue);

    NSTextField* excludeTfField = self.textFields[@(TextFieldExcludeTf)];
    std::string tf = cppString(excludeTfField.stringValue);
    tf = removeTemplateExpr(tf, oldExpr);
    t.setValue(newValue);
    if (enabled && !newExpr.empty())
        tf = appendTemplateExpr(tf, newExpr);
    else
        enabled = false;

    excludeTfField.stringValue = nsString(tf);
    lastfm::settings::setExcludedTitleFormatExpression(tf);
    self.templateCheckboxes[@(t.tag)].state = enabled ? NSControlStateValueOn : NSControlStateValueOff;
}

- (void)loadTextFields
{
    for (const auto& setting : kTextFields)
        self.textFields[@(setting.tag)].stringValue = nsString(setting.getValue());
}

- (void)loadTemplateSettings
{
    for (const auto& t : kTemplates)
        self.templateFields[@(t.tag)].stringValue = nsString(t.getValue());
    [self refreshTemplateCheckboxStates];
}

- (void)refreshDynamicSourcesEnabledState
{
    const BOOL enabled = self.onlyLibraryCheckbox.state != NSControlStateValueOn;

    self.dynamicPopup.enabled = enabled;
    self.dynamicSourcesLabel.enabled = enabled;
}

- (void)loadSettings
{
    [self.consolePopup selectItemAtIndex:lastfm::settings::consoleLevel()];
    self.disableNowPlayingCheckbox.state =
        lastfm::settings::disableNowPlaying() ? NSControlStateValueOn : NSControlStateValueOff;
    self.onlyLibraryCheckbox.state =
        lastfm::settings::onlyScrobbleFromMediaLibrary() ? NSControlStateValueOn : NSControlStateValueOff;
    [self.dynamicPopup selectItemAtIndex:lastfm::settings::configuredDynamicSourcesMode()];
    [self refreshDynamicSourcesEnabledState];
    self.treatVariousArtistsCheckbox.state =
        lastfm::settings::treatVariousArtistsAsEmpty() ? NSControlStateValueOn : NSControlStateValueOff;

    [self loadTextFields];

    [self loadTemplateSettings];
}

- (IBAction)onConsolePopup:(id)sender
{
    const int choice = static_cast<int>(self.consolePopup.indexOfSelectedItem);
    lastfm::settings::setConsoleLevel(choice);
    lastfmSetLogLevelFromConsoleChoice(choice);
}

- (IBAction)onDisableNowPlaying:(id)sender
{
    lastfm::settings::setDisableNowPlaying(self.disableNowPlayingCheckbox.state == NSControlStateValueOn);
}

- (IBAction)onOnlyLibrary:(id)sender
{
    lastfm::settings::setOnlyScrobbleFromMediaLibrary(self.onlyLibraryCheckbox.state == NSControlStateValueOn);
    [self refreshDynamicSourcesEnabledState];
}

- (IBAction)onDynamicPopup:(id)sender
{
    lastfm::settings::setDynamicSourcesMode(static_cast<int>(self.dynamicPopup.indexOfSelectedItem));
}

- (IBAction)onTreatVariousArtists:(id)sender
{
    lastfm::settings::setTreatVariousArtistsAsEmpty(self.treatVariousArtistsCheckbox.state == NSControlStateValueOn);
}

- (IBAction)onTextField:(id)sender
{
    [self applyTextField:sender];
}

- (IBAction)onTemplateCheckbox:(id)sender
{
    if (![sender isKindOfClass:[NSButton class]])
        return;

    NSButton* checkbox = sender;
    const ExclusionTemplate* t = findTemplate(checkbox.tag);
    if (t)
        [self applyTemplate:*t enabled:checkbox.state == NSControlStateValueOn value:self.templateFields[@(t->tag)].stringValue];
}

- (IBAction)onTemplateTextField:(id)sender
{
    if (![sender isKindOfClass:[NSTextField class]])
        return;

    NSTextField* field = sender;
    const ExclusionTemplate* t = findTemplate(field.tag);
    if (t)
        [self applyTemplate:*t enabled:self.templateCheckboxes[@(t->tag)].state == NSControlStateValueOn value:field.stringValue];
}

- (void)controlTextDidEndEditing:(NSNotification*)notification
{
    id object = notification.object;
    if ([object isKindOfClass:[NSTextField class]])
    {
        NSTextField* field = object;
        if (field.tag >= ExclusionTemplateGenre && field.tag <= ExclusionTemplateComment)
        {
            [self onTemplateTextField:field];
            return;
        }
    }

    [self applyTextField:object];
}

- (void)applyTextField:(id)sender
{
    if (![sender isKindOfClass:[NSTextField class]])
        return;

    NSTextField* field = sender;
    const std::string value = cppString(field.stringValue);
    for (const auto& setting : kTextFields)
    {
        if (setting.tag != field.tag)
            continue;
        setting.setValue(value);
        if (field.tag == TextFieldExcludeTf)
            [self refreshTemplateCheckboxStates];
        return;
    }
}

@end

namespace
{
class lastfm_preferences_page : public preferences_page_v2
{
  public:
    service_ptr instantiate() override
    {
        return fb2k::wrapNSObject([LastfmPreferencesController new]);
    }

    const char* get_name() override
    {
        return "Foo Scrobbler";
    }

    GUID get_guid() override
    {
        return GUID_LASTFM_PREFERENCES_PAGE;
    }

    GUID get_parent_guid() override
    {
        return preferences_page::guid_tools;
    }

    double get_sort_priority() override
    {
        return -50.0;
    }
};

FB2K_SERVICE_FACTORY(lastfm_preferences_page);
} // namespace
