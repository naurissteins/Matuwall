from __future__ import annotations

import os

import gi

gi.require_version("Gtk", "4.0")
gi.require_version("Adw", "1")

gi.require_version("Gdk", "4.0")

LAYER_SHELL_ERROR = None
try:
    gi.require_version("Gtk4LayerShell", "1.0")
    from gi.repository import Gtk4LayerShell as LayerShell  # type: ignore
except (ImportError, ValueError) as exc:
    LayerShell = None
    LAYER_SHELL_ERROR = str(exc)

from gi.repository import Adw, Gdk, Gtk


class PanelMixin:
    def _show_backdrop(self) -> None:
        if not self._backdrop_window:
            return
        if self._backdrop_window.get_visible():
            return
        self._backdrop_window.present()

    def _refresh_layer_shell(self) -> bool:
        if not self._panel_mode or not self._window:
            return False
        self._apply_layer_shell(
            self._window,
            self._panel_edge,
            self._panel_size,
            self._panel_thumbs_col,
            self._panel_margins,
        )
        return False

    def _on_close_request(self, _window: Gtk.Window) -> bool:
        if self._daemon_enabled:
            self._hide_window()
            return True
        if self._keep_ui_alive and os.environ.get("MATUWALL_UI") == "1":
            self._hide_window()
            return True
        if self._panel_mode and self._backdrop_enabled and self._backdrop_window:
            self._hide_window()
            return True
        return False

    @staticmethod
    def _is_wayland() -> bool:
        display = Gdk.Display.get_default()
        if not display:
            return False
        name = (display.get_name() or "").lower()
        if "wayland" in name:
            return True
        return False

    def _apply_layer_shell(
        self,
        window: Gtk.Window,
        panel_edge: str,
        panel_size: int,
        panel_thumbs_col: int,
        panel_margins: tuple[int, int, int, int],
    ) -> None:
        if LayerShell is None:
            return
        LayerShell.init_for_window(window)
        try:
            LayerShell.set_namespace(window, "matuwall")
        except Exception:
            pass
        try:
            if hasattr(LayerShell, "is_supported") and not LayerShell.is_supported():
                self._log("gtk4-layer-shell reports unsupported; trying anyway")
        except Exception:
            pass
        self._log("layer_shell init called")
        try:
            if hasattr(LayerShell, "is_layer_window"):
                self._log(f"layer_shell active: {LayerShell.is_layer_window(window)}")
        except Exception:
            pass
        LayerShell.set_layer(window, LayerShell.Layer.TOP)
        LayerShell.set_keyboard_mode(window, LayerShell.KeyboardMode.EXCLUSIVE)
        LayerShell.set_exclusive_zone(
            window, int(self.config.panel_exclusive_zone)
        )

        LayerShell.set_anchor(window, LayerShell.Edge.LEFT, False)
        LayerShell.set_anchor(window, LayerShell.Edge.RIGHT, False)
        LayerShell.set_anchor(window, LayerShell.Edge.TOP, False)
        LayerShell.set_anchor(window, LayerShell.Edge.BOTTOM, False)
        margin_top, margin_bottom, margin_left, margin_right = panel_margins
        LayerShell.set_margin(window, LayerShell.Edge.TOP, margin_top)
        LayerShell.set_margin(window, LayerShell.Edge.BOTTOM, margin_bottom)
        LayerShell.set_margin(window, LayerShell.Edge.LEFT, margin_left)
        LayerShell.set_margin(window, LayerShell.Edge.RIGHT, margin_right)

        monitor_width, monitor_height = self._get_primary_monitor_size()
        self._log(f"layer_shell monitor size: {monitor_width}x{monitor_height}")
        panel_thumbs_col = self._effective_panel_thumbs_col(
            panel_edge,
            panel_size,
            panel_thumbs_col,
            panel_margins,
            monitor_width,
            monitor_height,
        )
        self._panel_thumbs_col = panel_thumbs_col

        target_width, target_height = self._panel_target_size(
            panel_edge,
            panel_size,
            panel_thumbs_col,
            monitor_width,
            monitor_height,
            panel_margins,
        )

        # Centering logic: adjust margins to center the panel on the screen
        if panel_edge in {"left", "right"}:
            available = max(1, monitor_height - margin_top - margin_bottom)
            offset = max(0, (available - target_height) // 2)
            margin_top = margin_top + offset
            margin_bottom = margin_bottom + offset
        else:
            available = max(1, monitor_width - margin_left - margin_right)
            offset = max(0, (available - target_width) // 2)
            margin_left = margin_left + offset
            margin_right = margin_right + offset

        LayerShell.set_margin(window, LayerShell.Edge.TOP, margin_top)
        LayerShell.set_margin(window, LayerShell.Edge.BOTTOM, margin_bottom)
        LayerShell.set_margin(window, LayerShell.Edge.LEFT, margin_left)
        LayerShell.set_margin(window, LayerShell.Edge.RIGHT, margin_right)

        if panel_edge == "left":
            LayerShell.set_anchor(window, LayerShell.Edge.LEFT, True)
            LayerShell.set_anchor(window, LayerShell.Edge.TOP, True)
            self._set_layer_size(window, target_width, target_height)
        elif panel_edge == "right":
            LayerShell.set_anchor(window, LayerShell.Edge.RIGHT, True)
            LayerShell.set_anchor(window, LayerShell.Edge.TOP, True)
            self._set_layer_size(window, target_width, target_height)
        elif panel_edge == "top":
            LayerShell.set_anchor(window, LayerShell.Edge.TOP, True)
            LayerShell.set_anchor(window, LayerShell.Edge.LEFT, True)
            self._set_layer_size(window, target_width, target_height)
        else:
            LayerShell.set_anchor(window, LayerShell.Edge.BOTTOM, True)
            LayerShell.set_anchor(window, LayerShell.Edge.LEFT, True)
            self._set_layer_size(window, target_width, target_height)
        self._apply_panel_size_hint(window, target_width, target_height)

    def _ensure_backdrop_window(self) -> None:
        if self._backdrop_window:
            return
        backdrop = Adw.ApplicationWindow(application=self)
        backdrop.set_title("Matuwall Backdrop")
        backdrop.set_decorated(False)
        backdrop.set_resizable(False)
        backdrop.set_opacity(self._backdrop_opacity)
        backdrop.add_css_class("matuwall-backdrop")
        box = Gtk.Box()
        box.set_hexpand(True)
        box.set_vexpand(True)
        box.set_can_target(True)
        backdrop.set_content(box)
        if self._backdrop_click_to_close:
            click = Gtk.GestureClick()
            click.connect("pressed", self._on_backdrop_pressed)
            box.add_controller(click)
        self._backdrop_window = backdrop
        self._apply_backdrop_layer_shell(backdrop)

    def _apply_backdrop_layer_shell(self, window: Gtk.Window) -> None:
        if LayerShell is None:
            return
        LayerShell.init_for_window(window)
        try:
            LayerShell.set_namespace(window, "matuwall-backdrop")
        except Exception:
            pass
        LayerShell.set_layer(window, LayerShell.Layer.TOP)
        LayerShell.set_keyboard_mode(window, LayerShell.KeyboardMode.NONE)
        LayerShell.set_exclusive_zone(window, -1)
        LayerShell.set_anchor(window, LayerShell.Edge.LEFT, True)
        LayerShell.set_anchor(window, LayerShell.Edge.RIGHT, True)
        LayerShell.set_anchor(window, LayerShell.Edge.TOP, True)
        LayerShell.set_anchor(window, LayerShell.Edge.BOTTOM, True)
        monitor_width, monitor_height = self._get_primary_monitor_size()
        if monitor_width and monitor_height:
            self._set_layer_size(window, monitor_width, monitor_height)
        self._apply_panel_size_hint(window, monitor_width, monitor_height)

    def _on_backdrop_pressed(self, *_args) -> None:
        self._hide_window()

    def _set_layer_size(self, window: Gtk.Window, width: int, height: int) -> None:
        if LayerShell is None:
            return
        try:
            LayerShell.set_size(window, int(width), int(height))
        except Exception:
            return

    def _panel_target_size(
        self,
        panel_edge: str,
        panel_size: int,
        panel_thumbs_col: int,
        monitor_width: int,
        monitor_height: int,
        panel_margins: tuple[int, int, int, int],
    ) -> tuple[int, int]:
        item_w, item_h = self._get_item_outer_dimensions(
            panel_edge=panel_edge,
            panel_size=panel_size,
            apply_panel_full_width=True,
        )

        if panel_edge in {"left", "right"}:
            width = panel_size
            height = item_h * panel_thumbs_col + self.GRID_PADDING * 2
        else:
            height = panel_size
            width = item_w * panel_thumbs_col + self.GRID_PADDING * 2

        return int(max(1, width)), int(max(1, height))

    def _effective_panel_thumbs_col(
        self,
        panel_edge: str,
        panel_size: int,
        panel_thumbs_col: int,
        panel_margins: tuple[int, int, int, int],
        monitor_width: int,
        monitor_height: int,
    ) -> int:
        requested = max(1, int(panel_thumbs_col))
        margin_top, margin_bottom, margin_left, margin_right = panel_margins
        if panel_edge in {"left", "right"} and monitor_height <= 0:
            return requested
        if panel_edge in {"top", "bottom"} and monitor_width <= 0:
            return requested

        item_w, item_h = self._get_item_outer_dimensions(
            panel_edge=panel_edge,
            panel_size=panel_size,
            apply_panel_full_width=True,
        )
        if panel_edge in {"left", "right"}:
            available = max(
                1,
                monitor_height - margin_top - margin_bottom - self.GRID_PADDING * 2,
            )
            max_visible = max(1, available // max(1, item_h))
        else:
            available = max(
                1,
                monitor_width - margin_left - margin_right - self.GRID_PADDING * 2,
            )
            max_visible = max(1, available // max(1, item_w))

        return min(requested, max_visible)

    @staticmethod
    def _apply_panel_size_hint(window: Gtk.Window, width: int, height: int) -> None:
        if width > 0 and height > 0:
            try:
                window.set_default_size(int(width), int(height))
            except Exception:
                pass
            try:
                window.set_size_request(int(width), int(height))
            except Exception:
                pass

    def _get_item_outer_dimensions(
        self,
        panel_edge: str | None = None,
        panel_size: int | None = None,
        apply_panel_full_width: bool = True,
    ) -> tuple[int, int]:
        thumb_width, thumb_height = self._thumb_dimensions_for_layout(
            panel_edge=panel_edge,
            panel_size=panel_size,
            apply_panel_full_width=apply_panel_full_width,
        )
        item_w = thumb_width + (self.CARD_PADDING + self.CARD_BORDER + self.CARD_MARGIN) * 2
        item_h = thumb_height + (self.CARD_PADDING + self.CARD_BORDER + self.CARD_MARGIN) * 2
        return int(item_w), int(item_h)

    def _thumb_dimensions_for_layout(
        self,
        panel_edge: str | None = None,
        panel_size: int | None = None,
        apply_panel_full_width: bool = True,
    ) -> tuple[int, int]:
        if not self.config:
            return (1, 1)
        thumb_width = max(1, int(self.config.thumbnail_size))
        shape = (self.config.thumbnail_shape or "landscape").strip().lower()
        edge = str(panel_edge if panel_edge is not None else self._panel_edge).strip().lower()

        if (
            apply_panel_full_width
            and self._panel_mode
            and self._scroll_direction == "vertical"
            and edge in {"left", "right"}
        ):
            target_panel_size = (
                max(1, int(panel_size))
                if panel_size is not None
                else max(1, int(self._panel_size))
            )
            available = max(1, target_panel_size - self.GRID_PADDING * 2)
            thumb_width = max(1, available - self.CARD_PADDING * 2)

        if shape == "square":
            thumb_height = thumb_width
        else:
            thumb_height = max(1, int(thumb_width * self.LANDSCAPE_RATIO))
        return int(thumb_width), int(thumb_height)

    def _derive_panel_size(self, panel_edge: str) -> int:
        if not self.config:
            return 1
        item_w, item_h = self._get_item_outer_dimensions(
            panel_edge=panel_edge,
            apply_panel_full_width=False,
        )

        if panel_edge in {"left", "right"}:
            size = item_w + self.GRID_PADDING * 2
        else:
            size = item_h + self.GRID_PADDING * 2
        return max(1, int(size))

    @staticmethod
    def _get_primary_monitor_size() -> tuple[int, int]:
        display = Gdk.Display.get_default()
        if not display:
            return 0, 0
        monitor = None
        if hasattr(display, "get_primary_monitor"):
            try:
                monitor = display.get_primary_monitor()
            except Exception:
                monitor = None
        if monitor is None:
            monitors = display.get_monitors()
            if monitors and monitors.get_n_items() > 0:
                monitor = monitors.get_item(0)
        if monitor is None:
            return 0, 0
        geometry = monitor.get_geometry()
        return int(geometry.width), int(geometry.height)
