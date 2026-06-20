#ifndef KICKCAT_TOOLS_KICKUI_PANEL_H
#define KICKCAT_TOOLS_KICKUI_PANEL_H

namespace kickcat::kickui
{
    class BusSession;
    class Device;

    // A feature shown as one tab. The shell renders the tab only when
    // appliesTo() is true for the currently-selected device's capabilities
    // (e.g. has_coe, isMotor, later FoE/EoE). Adding a feature is one Panel
    // subclass + one registration in the shell. `session` is for bus-wide
    // queries; per-device control goes through `device`.
    //
    // Lifecycle: the shell creates one instance of every panel PER DEVICE and
    // destroys them together on disconnect/rescan, so an instance is bound to a
    // single device for its whole life -- per-device widget state is plain
    // members, no device-index-change guards.
    //
    // Data access: periodic state comes from session.snapshot(); discrete results
    // (SDO transfers, OD scans) from the session's event-fed getters.
    class Panel
    {
    public:
        virtual ~Panel() = default;

        virtual char const* title() const = 0;
        virtual bool appliesTo(Device const& device) const = 0;
        virtual void render(BusSession& session, Device& device) = 0;
    };
}

#endif
