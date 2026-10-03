#include "GUI/Host/GUIApplicationMenu.h"

#import <AppKit/AppKit.h>

#include <memory>
#include <utility>

// Boxes one item callback. Held by the item's representedObject (retained) so
// the target stays alive for the menu's lifetime; target/selector bypasses the
// responder chain, so the action fires even when no editor window is key.
// ObjC declarations must live at global scope (outside any C++ namespace).
@interface YaMenuItemActionBox : NSObject
- (instancetype)initWithAction:(std::function<void()>)action;
- (void)fire:(id)sender;
@end

@implementation YaMenuItemActionBox
{
    std::function<void()> _action;
}
- (instancetype)initWithAction:(std::function<void()>)action
{
    if ((self = [super init])) {
        _action = std::move(action);
    }
    return self;
}
- (void)fire:(id)__unused sender
{
    if (_action) {
        _action();
    }
}
@end

// Rebuilds menus from the provider whenever AppKit asks (menuNeedsUpdate
// fires before each display), so check/enabled states are always fresh.
// The main menu's top-level titles are refreshed there too; submenus rebuild
// their entries from the matching provider description.
@interface YaAppMenuDelegate : NSObject<NSMenuDelegate>
- (instancetype)initWithProvider:
    (std::function<std::vector<ya::FNativeMenuDesc>()>)provider;
@end

@implementation YaAppMenuDelegate
{
    std::function<std::vector<ya::FNativeMenuDesc>()> _provider;
}
- (instancetype)initWithProvider:(std::function<std::vector<ya::FNativeMenuDesc>()>)provider
{
    if ((self = [super init])) {
        _provider = std::move(provider);
    }
    return self;
}
- (void)menuNeedsUpdate:(NSMenu*)menu
{
    if (menu == NSApp.mainMenu) {
        [self rebuildTopLevel:menu];
    }
    else {
        [self rebuildEntries:menu];
    }
}
- (void)rebuildTopLevel:(NSMenu*)mainMenu
{
    if (!_provider) {
        return;
    }
    const std::vector<ya::FNativeMenuDesc> menus = _provider();
    // Keep item 0: the fixed app submenu (About/Hide/Quit). The provider's
    // menus follow it; appends only when the set changed so open menus do not
    // flicker on every hover.
    const NSUInteger existing = mainMenu.itemArray.count;
    const NSUInteger wanted   = static_cast<NSUInteger>(menus.size()) + 1;
    for (NSUInteger i = existing; i > wanted; --i) {
        [mainMenu removeItemAtIndex:i - 1];
    }
    for (size_t i = 1; i < menus.size() + 1; ++i) {
        const ya::FNativeMenuDesc& desc = menus[i - 1];
        NSString* title = [NSString stringWithUTF8String:desc.title.c_str()];
        if (i < existing) {
            NSMenuItem* item = [mainMenu itemAtIndex:i];
            if ([item.title isEqualToString:title]) {
                continue;
            }
            item.title = title;
            if (item.submenu) {
                item.submenu.title = title;
            }
            continue;
        }
        NSMenuItem* item = [mainMenu addItemWithTitle:title action:nil keyEquivalent:@""];
        NSMenu* sub = [[NSMenu alloc] initWithTitle:title];
        sub.autoenablesItems = NO;
        sub.delegate         = self;
        item.submenu         = sub;
    }
}
- (void)rebuildEntries:(NSMenu*)menu
{
    if (!_provider) {
        return;
    }
    const std::vector<ya::FNativeMenuDesc> menus = _provider();
    [menu removeAllItems];
    menu.autoenablesItems = NO;
    for (const ya::FNativeMenuDesc& desc : menus) {
        if (![menu.title isEqualToString:[NSString stringWithUTF8String:desc.title.c_str()]]) {
            continue;
        }
        for (const ya::FNativeMenuItem& entry : desc.buildItems()) {
            if (entry.bSeparator) {
                [menu addItem:[NSMenuItem separatorItem]];
                continue;
            }
            NSMenuItem* item = [menu addItemWithTitle:[NSString stringWithUTF8String:entry.label.c_str()]
                                               action:@selector(fire:)
                                        keyEquivalent:@""];
            YaMenuItemActionBox* box = [[YaMenuItemActionBox alloc] initWithAction:entry.action];
            item.representedObject = box;
            item.target            = box;
            item.state             = entry.bChecked ? NSControlStateValueOn : NSControlStateValueOff;
            item.enabled           = entry.bEnabled ? YES : NO;
        }
    }
}
@end

namespace ya
{
namespace
{

YaAppMenuDelegate* s_appMenuDelegate = nil;

NSMenu* makeMacOsAppSubMenu()
{
    // AppKit renders the FIRST main-menu submenu as the app menu (its title is
    // the process name); fill it with the standard app items.
    NSMenu* appMenu   = [[NSMenu alloc] initWithTitle:@"YA"];
    NSMenuItem* about = [appMenu addItemWithTitle:@"About YA Editor"
                                           action:@selector(orderFrontStandardAboutPanel:)
                                    keyEquivalent:@""];
    about.target = nil; // responder chain: NSApp owns the standard panels
    [appMenu addItem:[NSMenuItem separatorItem]];
    [appMenu addItemWithTitle:@"Hide YA" action:@selector(hide:) keyEquivalent:@"h"];
    [appMenu addItemWithTitle:@"Quit YA" action:@selector(terminate:) keyEquivalent:@"q"];
    return appMenu;
}

} // namespace

bool installApplicationMenu(std::function<std::vector<FNativeMenuDesc>()> buildMenus)
{
    if (!NSApp || !buildMenus) {
        return false;
    }
    s_appMenuDelegate = [[YaAppMenuDelegate alloc] initWithProvider:std::move(buildMenus)];

    NSMenu* mainMenu  = [[NSMenu alloc] initWithTitle:@"YA"];
    mainMenu.delegate = s_appMenuDelegate;
    mainMenu.autoenablesItems = NO;
    NSMenuItem* appMenuItem = [mainMenu addItemWithTitle:@"YA" action:nil keyEquivalent:@""];
    appMenuItem.submenu     = makeMacOsAppSubMenu();
    [s_appMenuDelegate rebuildTopLevel:mainMenu]; // provider titles after the app menu
    NSApp.mainMenu = mainMenu;
    return true;
}

void removeApplicationMenu()
{
    s_appMenuDelegate = nil;
    if (NSApp) {
        NSApp.mainMenu = nil;
    }
}

void refreshApplicationMenu()
{
    if (s_appMenuDelegate && NSApp.mainMenu) {
        [s_appMenuDelegate rebuildTopLevel:NSApp.mainMenu];
    }
}
} // namespace ya

