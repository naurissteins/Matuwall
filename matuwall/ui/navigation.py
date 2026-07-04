from __future__ import annotations

import gi

gi.require_version("Gtk", "4.0")

gi.require_version("Gdk", "4.0")

from gi.repository import Gdk, Gtk


class NavigationMixin:
    def _attach_navigation(self, grid_view: Gtk.GridView) -> None:
        grid_view.set_focusable(True)
        key_controller = Gtk.EventControllerKey()
        key_controller.set_propagation_phase(Gtk.PropagationPhase.CAPTURE)
        key_controller.connect("key-pressed", self._on_key_pressed)
        grid_view.add_controller(key_controller)

    def _on_key_pressed(
        self,
        _controller: Gtk.EventControllerKey,
        keyval: int,
        _keycode: int,
        _state: Gdk.ModifierType,
    ) -> bool:
        if keyval == Gdk.KEY_Escape:
            self._close_window()
            return True

        if keyval in (Gdk.KEY_Return, Gdk.KEY_KP_Enter):
            self._activate_selected_item()
            return True

        if keyval in (Gdk.KEY_Up, Gdk.KEY_k):
            return self._move_selection(0, -1)
        if keyval in (Gdk.KEY_Down, Gdk.KEY_j):
            return self._move_selection(0, 1)
        if keyval in (Gdk.KEY_Left, Gdk.KEY_h):
            return self._move_selection(-1, 0)
        if keyval in (Gdk.KEY_Right, Gdk.KEY_l):
            return self._move_selection(1, 0)

        return False

    def _move_selection(self, dx: int, dy: int) -> bool:
        grid = getattr(self, "_grid_view", None)
        if not grid:
            return False
        selection = grid.get_model()
        if not selection or not hasattr(selection, "set_selected"):
            return False
        
        index = selection.get_selected()
        store = getattr(self, "_list_store", None)
        if not store:
            return False
            
        n_items = store.get_n_items()
        if n_items == 0:
            return False

        if index == Gtk.INVALID_LIST_POSITION:
            selection.set_selected(0)
            return True

        actual_cols = grid.get_max_columns()
        if actual_cols < 1:
            actual_cols = 1
        
        # Orientation-aware logic for GridView
        is_horiz = getattr(self, "_scroll_direction", "vertical") == "horizontal"
        
        if is_horiz:
            # In horizontal GridView, max_columns refers to items per column (rows)
            # dx moves between columns, dy moves within a column
            row = index % actual_cols
            col = index // actual_cols
            new_row = row + dy
            new_col = col + dx
            new_index = new_col * actual_cols + new_row
        else:
            # In vertical GridView, max_columns refers to items per row (cols)
            # dx moves within a row, dy moves between rows
            row = index // actual_cols
            col = index % actual_cols
            new_row = row + dy
            new_col = col + dx
            new_index = new_row * actual_cols + new_col

        if 0 <= new_index < n_items:
            # Set flag for snapping logic in content.py
            self._is_keyboard_navigating = True
            selection.set_selected(new_index)
            return True
        
        return False

    def _close_window(self) -> None:
        window = getattr(self, "_window", None)
        if not window:
            return
        hide = getattr(self, "_hide_window", None)
        if callable(hide):
            hide()
        else:
            window.close()

    def _activate_selected_item(self) -> None:
        grid = getattr(self, "_grid_view", None)
        if not grid:
            return
        selection = grid.get_model()
        if not selection:
            return
        index = selection.get_selected()
        store = getattr(self, "_list_store", None)
        if store and index < store.get_n_items():
            item = store.get_item(index)
            if item:
                # Call _run_matugen from the app
                run_matugen = getattr(self, "_run_matugen", None)
                if callable(run_matugen):
                    from pathlib import Path
                    run_matugen(Path(item.path_str))
