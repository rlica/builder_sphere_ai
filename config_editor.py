#!/usr/bin/env python3

#RL: v1 provided by Gemini 2.5 Pro

import tkinter as tk
from tkinter import ttk, filedialog, messagebox
import json
import os

class JsonConfigEditor(ttk.Frame):
    """
    A GUI application for editing a nested JSON configuration file.
    The editor dynamically creates widgets based on the JSON structure
    and provides special handling for the 'detector' section which contains
    arrays of arrays for digitizer modules and channels.
    """

    def __init__(self, parent):
        super().__init__(parent)
        self.parent = parent
        self.parent.title("JSON Configuration Editor")
        self.parent.geometry("900x700")

        # --- Data Storage ---
        self.filepath = None
        self.data = {}
        self.widgets = {}

        # --- UI Setup ---
        self._setup_menu()
        
        self.main_frame = ttk.Frame(self.parent, padding="10")
        self.main_frame.pack(fill=tk.BOTH, expand=True)

        self.notebook = ttk.Notebook(self.main_frame)
        self.notebook.pack(fill=tk.BOTH, expand=True)

        # --- Initial Load ---
        # Try to load a 'config.json' file if it exists in the same directory.
        if os.path.exists("config.json"):
            self.load_file("config.json")
        else:
            messagebox.showinfo(
                "Welcome",
                "No 'config.json' found.\n"
                "Use 'File -> Open' to load a configuration file."
            )

    def _setup_menu(self):
        """Creates the main menu bar for the application."""
        self.menu_bar = tk.Menu(self.parent)
        self.parent.config(menu=self.menu_bar)

        file_menu = tk.Menu(self.menu_bar, tearoff=0)
        self.menu_bar.add_cascade(label="File", menu=file_menu)
        file_menu.add_command(label="Open...", command=self.open_file_dialog)
        file_menu.add_command(label="Save", command=self.save_file)
        file_menu.add_command(label="Save As...", command=lambda: self.save_file(save_as=True))
        file_menu.add_separator()
        file_menu.add_command(label="Exit", command=self.parent.quit)

    def open_file_dialog(self):
        """Opens a file dialog to select a JSON file to load."""
        path = filedialog.askopenfilename(
            title="Open Configuration File",
            filetypes=[("JSON files", "*.json"), ("All files", "*.*")]
        )
        if path:
            self.load_file(path)

    def load_file(self, filepath):
        """
        Loads and parses a JSON file, then populates the UI.
        
        Args:
            filepath (str): The path to the JSON file.
        """
        try:
            with open(filepath, 'r') as f:
                self.data = json.load(f)
            self.filepath = filepath
            self.parent.title(f"JSON Configuration Editor - {os.path.basename(filepath)}")
            self._populate_ui()
        except FileNotFoundError:
            messagebox.showerror("Error", f"File not found:\n{filepath}")
        except json.JSONDecodeError:
            messagebox.showerror("Error", f"Could not decode JSON from file:\n{filepath}")
        except Exception as e:
            messagebox.showerror("Error", f"An unexpected error occurred:\n{e}")

    def save_file(self, save_as=False):
        """
        Saves the current configuration data back to a JSON file.
        
        Args:
            save_as (bool): If True, forces a 'Save As' dialog.
        """
        if not self.filepath or save_as:
            path = filedialog.asksaveasfilename(
                title="Save Configuration File",
                defaultextension=".json",
                filetypes=[("JSON files", "*.json"), ("All files", "*.*")]
            )
            if not path:
                return  # User cancelled
            self.filepath = path
        
        try:
            self._update_data_from_widgets()
            with open(self.filepath, 'w') as f:
                json.dump(self.data, f, indent=4)
            self.parent.title(f"JSON Configuration Editor - {os.path.basename(self.filepath)}")
            messagebox.showinfo("Success", f"File saved successfully:\n{self.filepath}")
        except Exception as e:
            messagebox.showerror("Error", f"Failed to save file:\n{e}")

    def _populate_ui(self):
        """Clears and rebuilds the entire UI based on the self.data dictionary."""
        # Clear existing tabs and widgets
        for tab in self.notebook.tabs():
            self.notebook.forget(tab)
        self.widgets = {}

        # Create new tabs for each top-level key in the JSON
        for key, value in self.data.items():
            tab_frame = ttk.Frame(self.notebook, padding="10")
            self.notebook.add(tab_frame, text=key.capitalize())

            # Use a canvas with a scrollbar to handle potentially long content
            canvas = tk.Canvas(tab_frame)
            scrollbar = ttk.Scrollbar(tab_frame, orient="vertical", command=canvas.yview)
            scrollable_frame = ttk.Frame(canvas)

            scrollable_frame.bind(
                "<Configure>",
                lambda e: canvas.configure(scrollregion=canvas.bbox("all"))
            )
            canvas.create_window((0, 0), window=scrollable_frame, anchor="nw")
            canvas.configure(yscrollcommand=scrollbar.set)
            
            canvas.pack(side="left", fill="both", expand=True)
            scrollbar.pack(side="right", fill="y")

            # Special handling for the 'detector' tab
            if key == 'detector':
                self._create_detector_tab(scrollable_frame, value)
            else:
                self._create_widgets_recursively(scrollable_frame, value, key)

    def _create_widgets_recursively(self, parent, data, parent_key):
        """
        Recursively creates labels and entry widgets for nested dictionaries and lists.

        Args:
            parent (tk.Widget): The parent widget to place new widgets in.
            data (dict or list or value): The data to represent.
            parent_key (str): The dot-separated key path for this data level.
        """
        if isinstance(data, dict):
            for key, value in data.items():
                current_key = f"{parent_key}.{key}"
                if isinstance(value, (dict, list)):
                    # Create a labeled frame for nested objects/lists
                    frame = ttk.LabelFrame(parent, text=key, padding="10")
                    frame.pack(fill=tk.X, expand=True, pady=5, padx=5)
                    self._create_widgets_recursively(frame, value, current_key)
                else:
                    # Create a simple Label and Entry for a key-value pair
                    row_frame = ttk.Frame(parent)
                    row_frame.pack(fill=tk.X, expand=True, pady=2)
                    ttk.Label(row_frame, text=f"{key}:", width=20).pack(side=tk.LEFT)
                    entry = ttk.Entry(row_frame)
                    entry.insert(0, str(value))
                    entry.pack(side=tk.LEFT, fill=tk.X, expand=True)
                    self.widgets[current_key] = entry
        elif isinstance(data, list):
            # For lists, we serialize to a JSON string to preserve structure (e.g., nested lists).
            row_frame = ttk.Frame(parent)
            row_frame.pack(fill=tk.X, expand=True, pady=2)
            list_as_string = json.dumps(data)

            # Use a more user-friendly Text widget for lists in the 'gasp' section.
            if parent_key.startswith('gasp'):
                widget = tk.Text(row_frame, height=3, wrap=tk.WORD)
                widget.insert('1.0', list_as_string)
            else:
                widget = ttk.Entry(row_frame)
                widget.insert(0, list_as_string)
            
            widget.pack(side=tk.LEFT, fill=tk.X, expand=True)
            self.widgets[parent_key] = widget

    def _create_detector_tab(self, parent, detector_data):
        """
        Creates the specialized UI for the 'detector' configuration,
        handling multiple modules and their channel settings.
        
        Args:
            parent (tk.Widget): The parent widget (the 'detector' tab).
            detector_data (dict): The dictionary for the 'detector' key.
        """
        # --- Module Selection ---
        controls_frame = ttk.Frame(parent)
        controls_frame.pack(fill=tk.X, pady=10)
        ttk.Label(controls_frame, text="Select Digitizer Module:").pack(side=tk.LEFT, padx=5)

        # Get the number of modules (assuming all arrays are the same length)
        num_modules = len(detector_data.get('type', []))
        module_indices = [f"Module {i}" for i in range(num_modules)]

        self.module_selector = ttk.Combobox(controls_frame, values=module_indices, state="readonly")
        if module_indices:
            self.module_selector.current(0)
        self.module_selector.pack(side=tk.LEFT, padx=5)
        self.module_selector.bind("<<ComboboxSelected>>", self._update_detector_view)

        # --- Add/Remove Buttons ---
        ttk.Button(controls_frame, text="Add Module", command=self._add_module).pack(side=tk.LEFT, padx=5)
        ttk.Button(controls_frame, text="Remove Selected Module", command=self._remove_module).pack(side=tk.LEFT, padx=5)

        # --- Channel Data Display ---
        self.detector_channels_frame = ttk.Frame(parent, padding="10")
        self.detector_channels_frame.pack(fill=tk.BOTH, expand=True)
        
        # Initial population of the detector view
        self._update_detector_view()
    
    def _update_detector_view(self, event=None):
        """
        Clears and rebuilds the channel entry grid for the selected module.
        Called when a new module is selected from the combobox.
        """
        for widget in self.detector_channels_frame.winfo_children():
            widget.destroy()

        try:
            selected_index = self.module_selector.current()
            if selected_index < 0:
                return # No module selected
        except tk.TclError:
            return # Combobox is empty or widget destroyed
        
        # Main grid to hold the 4 categories (type, lut, etc.)
        main_grid = ttk.Frame(self.detector_channels_frame)
        main_grid.pack(fill=tk.BOTH, expand=True)
        main_grid.columnconfigure(0, weight=1)
        main_grid.columnconfigure(1, weight=1)

        # Create entry grids for each detector property
        row = 0
        col = 0
        for key, module_list in self.data.get('detector', {}).items():
            frame = ttk.LabelFrame(main_grid, text=key.capitalize(), padding=10)
            frame.grid(row=row, column=col, padx=5, pady=5, sticky="nsew")
            
            if selected_index < len(module_list):
                channel_data = module_list[selected_index]
                self._create_channel_grid(frame, key, selected_index, channel_data)
            
            col += 1
            if col > 1:
                col = 0
                row += 1

    def _create_channel_grid(self, parent, key, module_index, channel_data):
        """
        Creates a 4x4 grid of Entry widgets for 16 channels.

        Args:
            parent (tk.Widget): The parent LabelFrame for the grid.
            key (str): The detector property key (e.g., 'type', 'lut').
            module_index (int): The index of the current module.
            channel_data (list): The list of 16 values for the channels.
        """
        for i in range(16):
            row, col = divmod(i, 4)
            entry = ttk.Entry(parent, width=8, justify='center')
            if i < len(channel_data):
                entry.insert(0, str(channel_data[i]))
            entry.grid(row=row, column=col, padx=2, pady=2)
            
            # Store a reference to the widget for saving later
            widget_key = f"detector.{key}.{module_index}.{i}"
            self.widgets[widget_key] = entry
    
    def _add_module(self):
        """Adds a new module with default values to the detector config."""
        detector_conf = self.data.get('detector', {})
        for key, modules in detector_conf.items():
            # Add a new 16-channel array with default value 0
            modules.append([0] * 16)
        
        self._populate_ui() # Full refresh to update the combobox
        self.notebook.select(list(self.data.keys()).index('detector')) # Switch to detector tab
        self.module_selector.current(len(self.module_selector['values']) - 1) # Select the new module
        self._update_detector_view()

    def _remove_module(self):
        """Removes the currently selected module."""
        try:
            selected_index = self.module_selector.current()
            if selected_index < 0:
                messagebox.showwarning("Warning", "No module selected to remove.")
                return
        except tk.TclError:
             messagebox.showwarning("Warning", "No modules to remove.")
             return
        
        if messagebox.askyesno("Confirm", f"Are you sure you want to remove Module {selected_index}?"):
            detector_conf = self.data.get('detector', {})
            for key, modules in detector_conf.items():
                if selected_index < len(modules):
                    modules.pop(selected_index)
            
            self._populate_ui() # Full refresh is easiest
            if 'detector' in self.data:
                 self.notebook.select(list(self.data.keys()).index('detector'))


    def _update_data_from_widgets(self):
        """
        Iterates through the stored widgets and updates the self.data dictionary
        with their current values before saving.
        """
        for key_path, widget in self.widgets.items():
            try:
                # Get value from widget, handling both Text and Entry types
                if isinstance(widget, tk.Text):
                    value_str = widget.get("1.0", tk.END).strip()
                else: # It's a ttk.Entry
                    value_str = widget.get().strip()

                keys = key_path.split('.')
                current_level = self.data
                
                # Navigate down the data structure to the parent of the target value
                for key in keys[:-1]:
                    if key.isdigit():
                        current_level = current_level[int(key)]
                    else:
                        current_level = current_level[key]

                final_key = keys[-1]
                
                # Update the value based on its context and type
                if isinstance(current_level, list) and final_key.isdigit():
                    # This handles individual channel entries in the 'detector' tab
                    index = int(final_key)
                    if index < len(current_level):
                        current_level[index] = self._parse_value(value_str)

                elif isinstance(current_level, dict):
                    # This handles all other key-value pairs
                    original_value = current_level.get(final_key)

                    if isinstance(original_value, list):
                        # If the original was a list, parse the value from its JSON string representation
                        try:
                            current_level[final_key] = json.loads(value_str)
                        except json.JSONDecodeError:
                            print(f"Warning: Invalid format for list '{key_path}'. Changes not saved.")
                            messagebox.showwarning(
                                "Save Warning",
                                f"Could not save list for '{key_path}'.\n\n"
                                f"Please ensure it is a valid JSON list (e.g., [1, 2, \"abc\"])."
                            )
                    else:
                        # Otherwise, it's a simple scalar value
                        current_level[final_key] = self._parse_value(value_str)

            except (KeyError, IndexError) as e:
                print(f"Warning: Could not update data for widget '{key_path}'. Key/Index not found. Error: {e}")
            except Exception as e:
                print(f"Warning: An error occurred updating data for '{key_path}'. Error: {e}")

    def _parse_value(self, value_str):
        """
        Attempts to convert a string value to the most appropriate type
        (int, float, bool, or string).
        """
        value_str_lower = value_str.lower()
        if value_str_lower == 'true':
            return True
        if value_str_lower == 'false':
            return False
        try:
            return int(value_str)
        except ValueError:
            try:
                return float(value_str)
            except ValueError:
                return value_str


if __name__ == "__main__":
    root = tk.Tk()
    app = JsonConfigEditor(root)
    root.mainloop()

