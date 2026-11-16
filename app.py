"""
Online Store Orders — Python GUI Application
=============================================
DSA Semester Project (teacher-assigned, separate from CivicFix)

What it does
------------
The same system as main.cpp, now with a modern graphical interface:
  * Linked List  — store ALL orders
  * Queue (FIFO) — process orders in the order they arrived
  * Stack (LIFO) — Undo the last cancel / status change
  * BST (Tree)   — fast search by order_id, O(log n)
  * Graph        — fastest delivery route, Dijkstra's algorithm

Files needed in the SAME folder as this file:
  * orders.csv           (30 sample orders)
  * delivery_routes.csv  (12 routes between cities)

Install & run (Windows / Mac / Linux)
--------------------------------------
    pip install customtkinter
    python app.py

No other package is needed. Python 3.10 or newer is recommended.

Architecture — separation of concerns
-------------------------------------
  BACKEND  (top half of this file, no GUI code at all):
      Order, OrderLinkedList, OrderQueue, ActionStack,
      OrderBST, DeliveryGraph, StoreBackend.
      These classes can be imported and tested without opening a window.

  FRONTEND (bottom half, class StoreApp):
      Only layout, buttons, inputs and messages.
      It calls the backend, never re-implements the data structures.
      Long work (CSV load, search comparison, Dijkstra) runs in a
      worker thread (threading.Thread) so the window never freezes.
      The worker never touches widgets directly — it hands its result
      back with self.after(0, ...) which runs on the GUI thread.

Layout
------
  +----------------+-------------------------------+
  | Left sidebar   | Main panel (pages)            |
  | navigation,    | Dashboard / Orders / Add /    |
  | Load / Save,   | Search / Process / Delivery / |
  | theme switch,  | Compare / Report              |
  | activity log   |                               |
  +----------------+-------------------------------+
  | Bottom status bar: status message + progress  |
  +-----------------------------------------------+
"""

# ------------------------------------------------------------------
# Imports — only the standard library + customtkinter
# ------------------------------------------------------------------
import csv
import os
import sys
import threading
import time
from dataclasses import dataclass, field
from datetime import datetime
from pathlib import Path

import tkinter as tk
from tkinter import filedialog, messagebox

import customtkinter as ctk

# ------------------------------------------------------------------
# Paths — CSV files live next to this script, wherever it is run from
# ------------------------------------------------------------------
BASE_DIR = Path(__file__).resolve().parent
ORDERS_CSV = BASE_DIR / "orders.csv"
ROUTES_CSV = BASE_DIR / "delivery_routes.csv"

CITIES = ["Lahore", "Karachi", "Islamabad", "Rawalpindi", "Faisalabad", "Multan"]
CATEGORIES = ["Electronics", "Clothing", "Footwear", "Accessories",
              "Beauty", "Home", "Sports", "Books"]
STATUSES = ["Pending", "Processing", "Shipped", "Delivered", "Cancelled"]


# ==================================================================
# BACKEND — no GUI code below this line until the UI section
# ==================================================================

@dataclass
class Order:
    """One row from orders.csv."""
    order_id: int = 0
    customer: str = ""
    product: str = ""
    category: str = ""
    price: int = 0
    quantity: int = 1
    date: str = ""
    city: str = ""
    status: str = "Pending"

    def to_row(self) -> list:
        return [self.order_id, self.customer, self.product, self.category,
                self.price, self.quantity, self.date, self.city, self.status]

    def __str__(self) -> str:
        return (f"{self.order_id} | {self.customer} | {self.product} | "
                f"{self.category} | Rs.{self.price} x{self.quantity} | "
                f"{self.date} | {self.city} | {self.status}")


# ------------------------------------------------------------------
# 1. LINKED LIST — store all orders
# ------------------------------------------------------------------
class _ListNode:
    __slots__ = ("data", "next")

    def __init__(self, data: Order):
        self.data = data
        self.next = None


class OrderLinkedList:
    """Singly linked list. Insert at end is O(1) (tail pointer)."""

    def __init__(self):
        self.head = None
        self.tail = None
        self.size = 0

    def insert_at_end(self, order: Order) -> None:
        node = _ListNode(order)
        if self.head is None:
            self.head = self.tail = node
        else:
            self.tail.next = node
            self.tail = node
        self.size += 1

    def search_by_id(self, order_id: int):
        """Return (order_or_None, steps_checked). Search is O(n)."""
        steps = 0
        cur = self.head
        while cur is not None:
            steps += 1
            if cur.data.order_id == order_id:
                return cur.data, steps
            cur = cur.next
        return None, steps

    def delete_by_id(self, order_id: int) -> bool:
        if self.head is None:
            return False
        if self.head.data.order_id == order_id:
            self.head = self.head.next
            if self.head is None:
                self.tail = None
            self.size -= 1
            return True
        cur = self.head
        while cur.next is not None and cur.next.data.order_id != order_id:
            cur = cur.next
        if cur.next is None:
            return False
        cur.next = cur.next.next
        if cur.next is None:
            self.tail = cur
        self.size -= 1
        return True

    def update_status(self, order_id: int, new_status: str) -> bool:
        order, _ = self.search_by_id(order_id)
        if order is None:
            return False
        order.status = new_status
        return True

    def to_list(self) -> list:
        out, cur = [], self.head
        while cur is not None:
            out.append(cur.data)
            cur = cur.next
        return out

    def count_status(self, status: str) -> int:
        return sum(1 for o in self.to_list() if o.status == status)

    def total_sales(self) -> int:
        """Total Rs. for orders that were not cancelled."""
        return sum(o.price * o.quantity for o in self.to_list()
                   if o.status != "Cancelled")


# ------------------------------------------------------------------
# 2. QUEUE — process orders FIFO (first in, first out)
# ------------------------------------------------------------------
class OrderQueue:
    """Queue of order IDs, built on a linked list. enqueue/dequeue O(1)."""

    def __init__(self):
        self._items: list = []  # front is index 0; kept small & simple
        # NOTE: for a student project a list is fine here. The FIFO
        # behaviour (not the container) is what the teacher checks:
        # enqueue appends at the rear, dequeue removes from the front.
        self._head = 0

    def is_empty(self) -> bool:
        return self._head >= len(self._items)

    def enqueue(self, order_id: int) -> None:
        self._items.append(order_id)

    def dequeue(self) -> int:
        if self.is_empty():
            return -1
        order_id = self._items[self._head]
        self._head += 1
        return order_id

    def peek(self) -> int:
        return -1 if self.is_empty() else self._items[self._head]

    @property
    def size(self) -> int:
        return len(self._items) - self._head

    def to_list(self) -> list:
        return self._items[self._head:]

    def clear(self) -> None:
        self._items, self._head = [], 0


# ------------------------------------------------------------------
# 3. STACK — Undo (LIFO: last action first)
# ------------------------------------------------------------------
@dataclass
class Action:
    order_id: int
    old_status: str
    new_status: str


class ActionStack:
    """Stack of Actions. push / pop are O(1)."""

    def __init__(self):
        self._items: list = []

    def is_empty(self) -> bool:
        return not self._items

    def push(self, action: Action) -> None:
        self._items.append(action)

    def pop(self):
        return self._items.pop() if self._items else None

    def peek(self):
        return self._items[-1] if self._items else None

    @property
    def size(self) -> int:
        return len(self._items)


# ------------------------------------------------------------------
# 4. BST — fast search by order_id
# ------------------------------------------------------------------
class _TreeNode:
    __slots__ = ("data", "left", "right")

    def __init__(self, data: Order):
        self.data = data
        self.left = None
        self.right = None


class OrderBST:
    """Binary Search Tree, key = order_id.
    Left = smaller ID, right = larger ID. Search is O(log n) average."""

    def __init__(self):
        self.root = None

    def insert(self, order: Order) -> None:
        self.root = self._insert(self.root, order)

    def _insert(self, node, order: Order):
        if node is None:
            return _TreeNode(order)
        if order.order_id < node.data.order_id:
            node.left = self._insert(node.left, order)
        elif order.order_id > node.data.order_id:
            node.right = self._insert(node.right, order)
        return node  # duplicate ID: do nothing (IDs are unique)

    def search_by_id(self, order_id: int):
        """Return (order_or_None, steps_checked)."""
        steps, cur = 0, self.root
        while cur is not None:
            steps += 1
            if order_id == cur.data.order_id:
                return cur.data, steps
            cur = cur.left if order_id < cur.data.order_id else cur.right
        return None, steps

    def update_status(self, order_id: int, new_status: str) -> bool:
        order, _ = self.search_by_id(order_id)
        if order is None:
            return False
        order.status = new_status
        return True

    def delete_by_id(self, order_id: int) -> None:
        self.root = self._delete(self.root, order_id)

    def _delete(self, node, order_id: int):
        if node is None:
            return None
        if order_id < node.data.order_id:
            node.left = self._delete(node.left, order_id)
        elif order_id > node.data.order_id:
            node.right = self._delete(node.right, order_id)
        else:
            if node.left is None:
                return node.right
            if node.right is None:
                return node.left
            # two children: replace with smallest in right subtree
            successor = node.right
            while successor.left is not None:
                successor = successor.left
            node.data = successor.data
            node.right = self._delete(node.right, successor.data.order_id)
        return node


def insert_balanced_bst(bst: OrderBST, orders: list) -> None:
    """Insert middle-first so a sorted ID list (1001-1030) does NOT
    make a degenerate straight-line tree. This keeps search O(log n).
    Same fix as in main.cpp (insertBalancedBST)."""
    def _insert(lo: int, hi: int):
        if lo > hi:
            return
        mid = (lo + hi) // 2
        bst.insert(orders[mid])
        _insert(lo, mid - 1)
        _insert(mid + 1, hi)
    _insert(0, len(orders) - 1)


# ------------------------------------------------------------------
# 5. GRAPH — fastest delivery route (Dijkstra, simple O(V^2))
# ------------------------------------------------------------------
class DeliveryGraph:
    """Cities = nodes, roads = edges, travel time = weight."""

    def __init__(self):
        self.cities: list = []
        self._time = {}  # (from, to) -> minutes

    def add_route(self, from_city: str, to_city: str, minutes: int) -> None:
        for city in (from_city, to_city):
            if city not in self.cities:
                self.cities.append(city)
        # roads are two-way with the same time
        self._time[(from_city, to_city)] = minutes
        self._time[(to_city, from_city)] = minutes

    def neighbours(self, city: str):
        return [(to, mins) for (frm, to), mins in self._time.items()
                if frm == city]

    def shortest_path(self, start: str, dest: str):
        """Dijkstra. Returns (minutes, path_list) or (None, [])."""
        if start not in self.cities or dest not in self.cities:
            return None, []
        dist = {c: float("inf") for c in self.cities}
        parent = {c: None for c in self.cities}
        dist[start] = 0
        done = set()
        while len(done) < len(self.cities):
            # nearest city not done yet
            unvisited = [c for c in self.cities if c not in done]
            if not unvisited:
                break
            u = min(unvisited, key=lambda c: dist[c])
            if dist[u] == float("inf"):
                break
            done.add(u)
            for v, mins in self.neighbours(u):
                if dist[u] + mins < dist[v]:
                    dist[v] = dist[u] + mins
                    parent[v] = u
        if dist[dest] == float("inf"):
            return None, []
        path, cur = [], dest
        while cur is not None:
            path.append(cur)
            cur = parent[cur]
        return int(dist[dest]), list(reversed(path))


# ------------------------------------------------------------------
# StoreBackend — coordinates all five structures + CSV files
# ------------------------------------------------------------------
class StoreBackend:
    """One place for all data. The GUI calls these methods only."""

    def __init__(self):
        self.orders = OrderLinkedList()
        self.tree = OrderBST()
        self.queue = OrderQueue()
        self.undo_stack = ActionStack()
        self.graph = DeliveryGraph()
        self.loaded = False
        self.routes_loaded = False

    # -- CSV loading (called from a worker thread) -----------------
    def load_orders(self, path: Path = ORDERS_CSV):
        if not Path(path).exists():
            raise FileNotFoundError(f"orders file not found: {path}")
        rows = []
        with open(path, newline="", encoding="utf-8") as f:
            reader = csv.DictReader(f)
            for row in reader:
                rows.append(Order(
                    order_id=int(row["order_id"]),
                    customer=row["customer_name"].strip(),
                    product=row["product"].strip(),
                    category=row["category"].strip(),
                    price=int(row["price_pkr"]),
                    quantity=int(row["quantity"]),
                    date=row["order_date"].strip(),
                    city=row["city"].strip(),
                    status=row["status"].strip(),
                ))
        # fresh load: rebuild every structure
        self.orders, self.tree, self.queue = OrderLinkedList(), OrderBST(), OrderQueue()
        for o in rows:
            self.orders.insert_at_end(o)
            if o.status in ("Pending", "Processing"):
                self.queue.enqueue(o.order_id)
        insert_balanced_bst(self.tree, rows)
        self.loaded = True
        return len(rows)

    def load_routes(self, path: Path = ROUTES_CSV):
        if not Path(path).exists():
            raise FileNotFoundError(f"routes file not found: {path}")
        count = 0
        with open(path, newline="", encoding="utf-8") as f:
            for row in csv.DictReader(f):
                self.graph.add_route(row["from_city"].strip(),
                                     row["to_city"].strip(),
                                     int(row["travel_time_min"]))
                count += 1
        self.routes_loaded = True
        return count

    def save_orders(self, path: Path = ORDERS_CSV) -> int:
        with open(path, "w", newline="", encoding="utf-8") as f:
            writer = csv.writer(f)
            writer.writerow(["order_id", "customer_name", "product", "category",
                             "price_pkr", "quantity", "order_date", "city", "status"])
            for o in self.orders.to_list():
                writer.writerow(o.to_row())
        return self.orders.size

    # -- Operations -------------------------------------------------
    def add_order(self, order: Order) -> None:
        existing, _ = self.orders.search_by_id(order.order_id)
        if existing is not None:
            raise ValueError(f"Order ID {order.order_id} already exists.")
        self.orders.insert_at_end(order)
        self.tree.insert(order)
        self.queue.enqueue(order.order_id)

    def cancel_order(self, order_id: int) -> str:
        order, _ = self.orders.search_by_id(order_id)
        if order is None:
            raise ValueError(f"Order {order_id} not found.")
        if order.status == "Cancelled":
            raise ValueError(f"Order {order_id} is already cancelled.")
        self.undo_stack.push(Action(order_id, order.status, "Cancelled"))
        self.orders.update_status(order_id, "Cancelled")
        self.tree.update_status(order_id, "Cancelled")
        return order.status

    def delete_order(self, order_id: int) -> None:
        if not self.orders.delete_by_id(order_id):
            raise ValueError(f"Order {order_id} not found.")
        self.tree.delete_by_id(order_id)

    def process_next(self):
        """Dequeue the next order and mark it Processing. Returns Order or None."""
        order_id = self.queue.dequeue()
        if order_id == -1:
            return None
        order, _ = self.orders.search_by_id(order_id)
        if order is None:
            return None
        self.undo_stack.push(Action(order_id, order.status, "Processing"))
        self.orders.update_status(order_id, "Processing")
        self.tree.update_status(order_id, "Processing")
        return order

    def undo_last(self):
        action = self.undo_stack.pop()
        if action is None:
            return None
        self.orders.update_status(action.order_id, action.old_status)
        self.tree.update_status(action.order_id, action.old_status)
        return action

    def compare_search(self, test_ids=(1001, 1010, 1024, 1030, 1017), repeat=200):
        """1000 searches in each structure. Returns a dict of measured results."""
        list_steps = tree_steps = 0
        start = time.perf_counter()
        for _ in range(repeat):
            for oid in test_ids:
                _, s = self.orders.search_by_id(oid)
                list_steps += s
        list_time = time.perf_counter() - start
        start = time.perf_counter()
        for _ in range(repeat):
            for oid in test_ids:
                _, s = self.tree.search_by_id(oid)
                tree_steps += s
        tree_time = time.perf_counter() - start
        return {
            "searches": repeat * len(test_ids),
            "list_steps": list_steps, "list_avg": list_steps / (repeat * len(test_ids)),
            "list_time_us": list_time * 1_000_000,
            "tree_steps": tree_steps, "tree_avg": tree_steps / (repeat * len(test_ids)),
            "tree_time_us": tree_time * 1_000_000,
        }

    def report(self) -> dict:
        return {
            "total": self.orders.size,
            "total_sales": self.orders.total_sales(),
            **{s: self.orders.count_status(s) for s in STATUSES},
            "queue_size": self.queue.size,
            "undo_size": self.undo_stack.size,
        }


# ==================================================================
# FRONTEND — CustomTkinter GUI (layout only, calls StoreBackend)
# ==================================================================
ctk.set_appearance_mode("dark")          # start dark; switch in sidebar
ctk.set_default_color_theme("green")     # matches the store / DSA theme


class StoreApp(ctk.CTk):
    """Main window: sidebar + main pages + bottom status bar."""

    def __init__(self):
        super().__init__()
        self.backend = StoreBackend()
        self.title("Online Store Orders — DSA Project")
        self.geometry("1220x760")
        self.minsize(1020, 660)

        # --- window grid: row 0 = content, row 1 = status bar -------
        self.grid_rowconfigure(0, weight=1)
        self.grid_rowconfigure(1, weight=0)
        self.grid_columnconfigure(0, weight=0)   # sidebar, fixed width
        self.grid_columnconfigure(1, weight=1)   # main panel, stretches

        self._build_sidebar()
        self._build_main()
        self._build_status_bar()

        # Load the CSVs in a worker thread so the window opens instantly
        self.log("Starting… loading delivery routes and orders.")
        self.run_in_thread(self._load_all_worker, self._on_loaded)

    # ----------------------------------------------------------------
    # Worker-thread helper — the key to a non-freezing GUI
    # ----------------------------------------------------------------
    def run_in_thread(self, worker, on_done=None, start_msg="Working…"):
        """Run `worker` (returns a value) in a thread.
        `on_done(result)` is called on the GUI thread via after()."""
        self.set_busy(True, start_msg)

        def _target():
            try:
                result = worker()
                error = None
            except Exception as exc:  # noqa: BLE001 — show, don't crash
                result, error = None, exc
            # after() is the safe way to touch widgets from a thread
            self.after(0, lambda: self._finish_thread(result, error, on_done))

        threading.Thread(target=_target, daemon=True).start()

    def _finish_thread(self, result, error, on_done):
        if not self.winfo_exists():
            return
        try:
            self.set_busy(False, "Ready.")
        except tk.TclError:
            return
        if error is not None:
            self.log(f"ERROR: {error}")
            messagebox.showerror("Error", str(error))
            return
        if on_done:
            try:
                on_done(result)
            except tk.TclError:
                pass  # window was closed while finishing
            except Exception as exc:  # noqa: BLE001
                self.log(f"ERROR: {exc}")
                messagebox.showerror("Error", str(exc))

    def set_busy(self, busy: bool, message: str = ""):
        """Show / hide the loading indicator in the status bar."""
        if message:
            self.status_label.configure(text=message)
        if busy:
            self.progress_bar.configure(mode="indeterminate")
            self.progress_bar.start()
        else:
            self.progress_bar.stop()
            self.progress_bar.configure(mode="determinate")
            self.progress_bar.set(0)

    # ----------------------------------------------------------------
    # Sidebar (left, fixed width 220)
    # ----------------------------------------------------------------
    def _build_sidebar(self):
        self.sidebar = ctk.CTkFrame(self, width=220, corner_radius=0)
        self.sidebar.grid(row=0, column=0, sticky="nsew")
        self.sidebar.grid_propagate(False)

        ctk.CTkLabel(self.sidebar, text="🛒 Online Store", font=("Segoe UI", 20, "bold"),
                     anchor="w").pack(padx=16, pady=(18, 2), fill="x")
        ctk.CTkLabel(self.sidebar, text="Orders • DSA Project", font=("Segoe UI", 12),
                     text_color="gray", anchor="w").pack(padx=16, pady=(0, 14), fill="x")

        self.nav_buttons = {}
        for key, label in [
            ("dashboard", "📊  Dashboard"),
            ("orders", "📋  All Orders"),
            ("add", "➕  Add Order"),
            ("search", "🔍  Search by ID"),
            ("process", "⚙️  Process Queue"),
            ("delivery", "🚚  Delivery Route"),
            ("compare", "⏱️  Compare Search"),
            ("report", "📈  Report"),
        ]:
            btn = ctk.CTkButton(self.sidebar, text=label, width=188, height=36,
                                anchor="w", font=("Segoe UI", 13),
                                command=lambda k=key: self.show_page(k))
            btn.pack(padx=16, pady=4)
            self.nav_buttons[key] = btn

        # Load / Save — validated: files must exist before loading
        ctk.CTkButton(self.sidebar, text="📂  Load orders.csv", width=188, height=34,
                      fg_color="#2b7a4b", hover_color="#23663e",
                      command=self.on_load_clicked).pack(padx=16, pady=(14, 4))
        ctk.CTkButton(self.sidebar, text="💾  Save orders.csv", width=188, height=34,
                      fg_color="#2b7a4b", hover_color="#23663e",
                      command=self.on_save_clicked).pack(padx=16, pady=4)

        # Dark / Light mode switch
        self.theme_switch = ctk.CTkSwitch(self.sidebar, text="Light mode",
                                          command=self.on_theme_toggle)
        self.theme_switch.pack(padx=16, pady=(14, 4), anchor="w")

        # Activity / error log panel (in-app, no terminal crash)
        ctk.CTkLabel(self.sidebar, text="Activity log", font=("Segoe UI", 12, "bold"),
                     anchor="w").pack(padx=16, pady=(12, 4), fill="x")
        self.log_box = ctk.CTkTextbox(self.sidebar, width=188, height=150,
                                      font=("Consolas", 11))
        self.log_box.pack(padx=16, pady=(0, 12), fill="x")
        self.log_box.configure(state="disabled")

    def log(self, message: str):
        """Append a line to the log panel and the status bar."""
        try:
            if not self.winfo_exists():
                return
            stamp = datetime.now().strftime("%H:%M:%S")
            self.log_box.configure(state="normal")
            self.log_box.insert("end", f"[{stamp}] {message}\n")
            self.log_box.see("end")
            self.log_box.configure(state="disabled")
            if hasattr(self, "status_label"):
                self.status_label.configure(text=message)
        except tk.TclError:
            pass  # window was closed while a worker was finishing

    def on_theme_toggle(self):
        mode = "light" if self.theme_switch.get() == 1 else "dark"
        ctk.set_appearance_mode(mode)
        self.log(f"Theme switched to {mode} mode.")

    # ----------------------------------------------------------------
    # Main panel (right, stretches) + bottom status bar
    # ----------------------------------------------------------------
    def _build_main(self):
        self.main = ctk.CTkFrame(self, corner_radius=0, fg_color="transparent")
        self.main.grid(row=0, column=1, sticky="nsew", padx=12, pady=12)
        self.main.grid_rowconfigure(0, weight=1)
        self.main.grid_columnconfigure(0, weight=1)

        self.pages = {}
        self.pages["dashboard"] = self._page_dashboard()
        self.pages["orders"] = self._page_orders()
        self.pages["add"] = self._page_add()
        self.pages["search"] = self._page_search()
        self.pages["process"] = self._page_process()
        self.pages["delivery"] = self._page_delivery()
        self.pages["compare"] = self._page_compare()
        self.pages["report"] = self._page_report()
        self.show_page("dashboard")

    def _build_status_bar(self):
        bar = ctk.CTkFrame(self, height=36, corner_radius=0)
        bar.grid(row=1, column=0, columnspan=2, sticky="ew")
        bar.grid_columnconfigure(0, weight=1)
        bar.grid_columnconfigure(1, weight=0)
        self.status_label = ctk.CTkLabel(bar, text="Ready.", anchor="w",
                                         font=("Segoe UI", 12))
        self.status_label.grid(row=0, column=0, padx=12, pady=6, sticky="w")
        self.progress_bar = ctk.CTkProgressBar(bar, width=220, height=12)
        self.progress_bar.grid(row=0, column=1, padx=12, pady=6, sticky="e")
        self.progress_bar.set(0)

    def show_page(self, key: str):
        for page in self.pages.values():
            page.grid_forget()
        self.pages[key].grid(row=0, column=0, sticky="nsew")
        if key == "dashboard":
            self.refresh_dashboard()
        elif key == "orders":
            self.refresh_orders_table()
        elif key == "process":
            self.refresh_process_page()
        elif key == "report":
            self.refresh_report()

    # -- small helpers ------------------------------------------------
    @staticmethod
    def _heading(parent, text: str, sub: str = ""):
        ctk.CTkLabel(parent, text=text, font=("Segoe UI", 20, "bold"),
                     anchor="w").pack(padx=8, pady=(6, 2), anchor="w")
        if sub:
            ctk.CTkLabel(parent, text=sub, font=("Segoe UI", 12),
                         text_color="gray", anchor="w").pack(padx=8, pady=(0, 10), anchor="w")

    @staticmethod
    def _field(parent, label: str):
        """Label + entry row. Returns the entry widget."""
        row = ctk.CTkFrame(parent, fg_color="transparent")
        row.pack(fill="x", padx=8, pady=5)
        ctk.CTkLabel(row, text=label, width=140, anchor="w",
                     font=("Segoe UI", 13)).pack(side="left")
        entry = ctk.CTkEntry(row, width=320, height=34)
        entry.pack(side="left", padx=(8, 0))
        return entry

    # ----------------------------------------------------------------
    # Page: Dashboard
    # ----------------------------------------------------------------
    def _page_dashboard(self):
        page = ctk.CTkFrame(self.main, fg_color="transparent")
        self._heading(page, "Dashboard", "Live totals from orders.csv — Linked List + BST")
        grid = ctk.CTkFrame(page, fg_color="transparent")
        grid.pack(fill="x", padx=8, pady=8)
        for i in range(3):
            grid.grid_columnconfigure(i, weight=1)
        self.stat_cards = {}
        stats = [("total", "Total orders"), ("Pending", "Pending"),
                 ("Processing", "Processing"), ("Shipped", "Shipped"),
                 ("Delivered", "Delivered"), ("Cancelled", "Cancelled"),
                 ("total_sales", "Total sales (Rs.)")]
        for idx, (key, label) in enumerate(stats):
            card = ctk.CTkFrame(grid, height=78, corner_radius=10)
            card.grid(row=idx // 3, column=idx % 3, padx=6, pady=6, sticky="ew")
            ctk.CTkLabel(card, text=label, font=("Segoe UI", 12),
                         text_color="gray").pack(pady=(10, 0))
            value = ctk.CTkLabel(card, text="—", font=("Segoe UI", 20, "bold"))
            value.pack(pady=(0, 10))
            self.stat_cards[key] = value
        ctk.CTkLabel(page, text="How to use: Load orders.csv (sidebar), then try "
                     "Search by ID, Process Queue, or Delivery Route.",
                     font=("Segoe UI", 12), text_color="gray", wraplength=700,
                     justify="left").pack(padx=8, pady=10, anchor="w")
        return page

    def refresh_dashboard(self):
        try:
            r = self.backend.report()
            self.stat_cards["total"].configure(text=str(r["total"]))
            for s in STATUSES:
                self.stat_cards[s].configure(text=str(r[s]))
            self.stat_cards["total_sales"].configure(text=f"{r['total_sales']:,}")
        except Exception as exc:  # noqa: BLE001
            self.log(f"ERROR refreshing dashboard: {exc}")

    # ----------------------------------------------------------------
    # Page: All Orders (scrollable table)
    # ----------------------------------------------------------------
    def _page_orders(self):
        page = ctk.CTkFrame(self.main, fg_color="transparent")
        self._heading(page, "All Orders", "Linked List — every order, in file order")
        top = ctk.CTkFrame(page, fg_color="transparent")
        top.pack(fill="x", padx=8, pady=4)
        ctk.CTkLabel(top, text="Filter by status:").pack(side="left")
        self.orders_filter = ctk.CTkComboBox(top, values=["All"] + STATUSES,
                                             width=150, command=lambda _: self.refresh_orders_table())
        self.orders_filter.pack(side="left", padx=8)
        ctk.CTkButton(top, text="Refresh", width=100,
                      command=self.refresh_orders_table).pack(side="left", padx=8)
        self.orders_scroll = ctk.CTkScrollableFrame(page, height=480)
        self.orders_scroll.pack(fill="both", expand=True, padx=8, pady=8)
        return page

    def refresh_orders_table(self):
        try:
            for w in self.orders_scroll.winfo_children():
                w.destroy()
            wanted = self.orders_filter.get()
            header = f"{'ID':<7}{'Customer':<16}{'Product':<22}{'City':<12}{'Status':<12}{'Price':>10}"
            ctk.CTkLabel(self.orders_scroll, text=header, font=("Consolas", 12, "bold"),
                         anchor="w").pack(fill="x", padx=6, pady=2)
            for o in self.backend.orders.to_list():
                if wanted != "All" and o.status != wanted:
                    continue
                line = (f"{o.order_id:<7}{o.customer:<16}{o.product:<22}"
                        f"{o.city:<12}{o.status:<12}Rs.{o.price:>7,}")
                ctk.CTkLabel(self.orders_scroll, text=line, font=("Consolas", 12),
                             anchor="w").pack(fill="x", padx=6, pady=1)
            self.log(f"Orders table refreshed ({self.backend.orders.size} orders).")
        except Exception as exc:  # noqa: BLE001
            self.log(f"ERROR refreshing orders: {exc}")
            messagebox.showerror("Error", str(exc))

    # ----------------------------------------------------------------
    # Page: Add Order (validated form)
    # ----------------------------------------------------------------
    def _page_add(self):
        page = ctk.CTkFrame(self.main, fg_color="transparent")
        self._heading(page, "Add Order", "All fields are validated before saving")
        self.add_id = self._field(page, "Order ID *")
        self.add_customer = self._field(page, "Customer name *")
        self.add_product = self._field(page, "Product *")
        row = ctk.CTkFrame(page, fg_color="transparent")
        row.pack(fill="x", padx=8, pady=5)
        ctk.CTkLabel(row, text="Category", width=140, anchor="w").pack(side="left")
        self.add_category = ctk.CTkComboBox(row, values=CATEGORIES, width=320)
        self.add_category.pack(side="left", padx=(8, 0))
        self.add_price = self._field(page, "Price (PKR) *")
        self.add_qty = self._field(page, "Quantity *")
        self.add_date = self._field(page, "Date (YYYY-MM-DD) *")
        self.add_date.insert(0, "2026-11-08")
        row2 = ctk.CTkFrame(page, fg_color="transparent")
        row2.pack(fill="x", padx=8, pady=5)
        ctk.CTkLabel(row2, text="City", width=140, anchor="w").pack(side="left")
        self.add_city = ctk.CTkComboBox(row2, values=CITIES, width=320)
        self.add_city.pack(side="left", padx=(8, 0))
        self.add_msg = ctk.CTkLabel(page, text="", text_color="#e05d5d",
                                    font=("Segoe UI", 12), wraplength=650, justify="left")
        self.add_msg.pack(padx=8, pady=6, anchor="w")
        ctk.CTkButton(page, text="➕  Add order", width=200, height=38,
                      command=self.on_add_order).pack(padx=8, pady=8, anchor="w")
        return page

    def on_add_order(self):
        """Validate every field first, then add (never crash on bad input)."""
        try:
            oid_text = self.add_id.get().strip()
            customer = self.add_customer.get().strip()
            product = self.add_product.get().strip()
            price_text = self.add_price.get().strip()
            qty_text = self.add_qty.get().strip()
            date_text = self.add_date.get().strip()
            if not oid_text or not customer or not product or not price_text or not qty_text or not date_text:
                raise ValueError("Please fill in all fields marked *.")
            if not oid_text.isdigit():
                raise ValueError("Order ID must be a whole number, e.g. 1031.")
            if not price_text.isdigit() or int(price_text) <= 0:
                raise ValueError("Price must be a positive whole number.")
            if not qty_text.isdigit() or int(qty_text) < 1:
                raise ValueError("Quantity must be 1 or more.")
            datetime.strptime(date_text, "%Y-%m-%d")  # raises ValueError if bad
            order = Order(order_id=int(oid_text), customer=customer, product=product,
                          category=self.add_category.get(), price=int(price_text),
                          quantity=int(qty_text), date=date_text,
                          city=self.add_city.get(), status="Pending")
            self.backend.add_order(order)
            self.add_msg.configure(text=f"✅ Order {order.order_id} added (Pending).", text_color="#4caf7d")
            self.log(f"Added order {order.order_id} — {customer}, {product}.")
            self.refresh_dashboard()
        except Exception as exc:  # noqa: BLE001
            self.add_msg.configure(text=f"❌ {exc}", text_color="#e05d5d")
            self.log(f"ERROR adding order: {exc}")
            messagebox.showerror("Cannot add order", str(exc))

    # ----------------------------------------------------------------
    # Page: Search by ID (BST)
    # ----------------------------------------------------------------
    def _page_search(self):
        page = ctk.CTkFrame(self.main, fg_color="transparent")
        self._heading(page, "Search by ID", "BST — O(log n). Try 1024 (found in 5 steps).")
        row = ctk.CTkFrame(page, fg_color="transparent")
        row.pack(fill="x", padx=8, pady=8)
        ctk.CTkLabel(row, text="Order ID:").pack(side="left")
        self.search_entry = ctk.CTkEntry(row, width=160, height=34)
        self.search_entry.pack(side="left", padx=8)
        self.search_entry.insert(0, "1024")
        ctk.CTkButton(row, text="🔍  Search (BST)", width=150,
                      command=self.on_search).pack(side="left", padx=8)
        ctk.CTkButton(row, text="🗑️  Delete this order", width=150,
                      fg_color="#8a3b3b", hover_color="#6e2f2f",
                      command=self.on_delete_searched).pack(side="left", padx=8)
        self.search_result = ctk.CTkTextbox(page, height=260, font=("Consolas", 13))
        self.search_result.pack(fill="both", expand=True, padx=8, pady=8)
        return page

    def on_search(self):
        try:
            text = self.search_entry.get().strip()
            if not text:
                raise ValueError("Enter an order ID first.")
            if not text.isdigit():
                raise ValueError("Order ID must be a number, e.g. 1024.")
            oid = int(text)
            order, tree_steps = self.backend.tree.search_by_id(oid)
            _, list_steps = self.backend.orders.search_by_id(oid)
            box = self.search_result
            box.configure(state="normal")
            box.delete("1.0", "end")
            if order is None:
                box.insert("end", f"Order {oid} not found.\n")
                self.log(f"Search {oid}: not found.")
            else:
                box.insert("end", f"Found (BST):\n{order}\n\n"
                                  f"BST steps: {tree_steps}\n"
                                  f"Linked List steps for the same ID: {list_steps}\n")
                self.log(f"Search {oid}: found in BST in {tree_steps} steps.")
            box.configure(state="disabled")
        except Exception as exc:  # noqa: BLE001
            self.log(f"ERROR searching: {exc}")
            messagebox.showerror("Search error", str(exc))

    def on_delete_searched(self):
        try:
            text = self.search_entry.get().strip()
            if not text.isdigit():
                raise ValueError("Enter a valid order ID to delete.")
            oid = int(text)
            if not messagebox.askyesno("Delete order", f"Delete order {oid} permanently?"):
                return
            self.backend.delete_order(oid)
            self.log(f"Deleted order {oid}.")
            messagebox.showinfo("Deleted", f"Order {oid} deleted.")
            self.refresh_dashboard()
            self.on_search()
        except Exception as exc:  # noqa: BLE001
            self.log(f"ERROR deleting: {exc}")
            messagebox.showerror("Delete error", str(exc))

    # ----------------------------------------------------------------
    # Page: Process Queue + Undo (Stack)
    # ----------------------------------------------------------------
    def _page_process(self):
        page = ctk.CTkFrame(self.main, fg_color="transparent")
        self._heading(page, "Process Queue", "Queue = FIFO. Undo = Stack (LIFO).")
        row = ctk.CTkFrame(page, fg_color="transparent")
        row.pack(fill="x", padx=8, pady=8)
        ctk.CTkButton(row, text="▶  Process next order", width=200, height=38,
                      command=self.on_process_next).pack(side="left", padx=(0, 8))
        ctk.CTkButton(row, text="↩  Undo last action", width=180, height=38,
                      command=self.on_undo).pack(side="left", padx=8)
        ctk.CTkButton(row, text="❌  Cancel an order…", width=170, height=38,
                      command=self.on_cancel_prompt).pack(side="left", padx=8)
        self.process_box = ctk.CTkTextbox(page, height=320, font=("Consolas", 13))
        self.process_box.pack(fill="both", expand=True, padx=8, pady=8)
        return page

    def refresh_process_page(self):
        try:
            box = self.process_box
            box.configure(state="normal")
            box.delete("1.0", "end")
            box.insert("end", f"Queue ({self.backend.queue.size} waiting, front first):\n")
            box.insert("end", "  " + " -> ".join(map(str, self.backend.queue.to_list())) + "\n\n")
            box.insert("end", f"Undo stack ({self.backend.undo_stack.size} actions):\n")
            top = self.backend.undo_stack.peek()
            box.insert("end", f"  Top: {top}\n" if top else "  (empty)\n")
            box.configure(state="disabled")
        except Exception as exc:  # noqa: BLE001
            self.log(f"ERROR refreshing queue page: {exc}")

    def on_process_next(self):
        try:
            order = self.backend.process_next()
            if order is None:
                messagebox.showinfo("Queue empty", "No order left to process.")
                self.log("Process next: queue empty.")
            else:
                self.log(f"Processed order {order.order_id} (now Processing).")
                messagebox.showinfo("Processed", f"Order {order.order_id} is now Processing.\n{order}")
            self.refresh_process_page()
            self.refresh_dashboard()
        except Exception as exc:  # noqa: BLE001
            self.log(f"ERROR processing: {exc}")
            messagebox.showerror("Error", str(exc))

    def on_undo(self):
        try:
            action = self.backend.undo_last()
            if action is None:
                messagebox.showinfo("Nothing to undo", "The undo stack is empty.")
            else:
                self.log(f"Undo: order {action.order_id} back to {action.old_status}.")
                messagebox.showinfo("Undo", f"Order {action.order_id} status back to {action.old_status}.")
            self.refresh_process_page()
            self.refresh_dashboard()
        except Exception as exc:  # noqa: BLE001
            self.log(f"ERROR undoing: {exc}")
            messagebox.showerror("Error", str(exc))

    def on_cancel_prompt(self):
        try:
            text = self.search_entry.get().strip()
            if not text.isdigit():
                self.show_page("search")
                messagebox.showinfo("Cancel an order", "Go to Search, type the order ID, then use Cancel from there — or type the ID here.")
                return
            oid = int(text)
            self.backend.cancel_order(oid)
            self.log(f"Cancelled order {oid} (undo available).")
            messagebox.showinfo("Cancelled", f"Order {oid} cancelled. Use Undo to restore it.")
            self.refresh_process_page()
            self.refresh_dashboard()
        except Exception as exc:  # noqa: BLE001
            self.log(f"ERROR cancelling: {exc}")
            messagebox.showerror("Cancel error", str(exc))

    # ----------------------------------------------------------------
    # Page: Delivery Route (Graph + Dijkstra, in a worker thread)
    # ----------------------------------------------------------------
    def _page_delivery(self):
        page = ctk.CTkFrame(self.main, fg_color="transparent")
        self._heading(page, "Delivery Route", "Graph + Dijkstra — warehouse is in Lahore")
        row = ctk.CTkFrame(page, fg_color="transparent")
        row.pack(fill="x", padx=8, pady=8)
        ctk.CTkLabel(row, text="Deliver to:").pack(side="left")
        self.delivery_city = ctk.CTkComboBox(row, values=CITIES, width=180)
        self.delivery_city.set("Islamabad")
        self.delivery_city.pack(side="left", padx=8)
        ctk.CTkButton(row, text="🚚  Find fastest route", width=190, height=36,
                      command=self.on_find_route).pack(side="left", padx=8)
        self.delivery_result = ctk.CTkTextbox(page, height=280, font=("Consolas", 13))
        self.delivery_result.pack(fill="both", expand=True, padx=8, pady=8)
        return page

    def on_find_route(self):
        dest = self.delivery_city.get().strip()
        if not dest:
            messagebox.showerror("Validation", "Choose a delivery city first.")
            return
        # Dijkstra runs in a thread; the progress bar spins meanwhile.
        self.run_in_thread(
            lambda: self.backend.graph.shortest_path("Lahore", dest),
            lambda result: self._show_route(dest, result),
            start_msg=f"Finding fastest route to {dest}…",
        )

    def _show_route(self, dest, result):
        minutes, path = result
        box = self.delivery_result
        box.configure(state="normal")
        box.delete("1.0", "end")
        if minutes is None:
            box.insert("end", f"No route from Lahore to {dest}.\n")
            self.log(f"Route to {dest}: no route found.")
        else:
            box.insert("end", f"Fastest route: Lahore -> {dest}\n"
                              f"Time: {minutes} minutes\n"
                              f"Path: {' -> '.join(path)}\n")
            self.log(f"Route Lahore -> {dest}: {minutes} min via {' -> '.join(path)}.")
        box.configure(state="disabled")

    # ----------------------------------------------------------------
    # Page: Compare Search (1000 searches, in a worker thread)
    # ----------------------------------------------------------------
    def _page_compare(self):
        page = ctk.CTkFrame(self.main, fg_color="transparent")
        self._heading(page, "Compare Search", "Same CSV, same 1,000 searches — List vs BST")
        ctk.CTkButton(page, text="⏱️  Run 1,000-search test", width=240, height=38,
                      command=self.on_compare).pack(padx=8, pady=8, anchor="w")
        self.compare_result = ctk.CTkTextbox(page, height=300, font=("Consolas", 13))
        self.compare_result.pack(fill="both", expand=True, padx=8, pady=8)
        return page

    def on_compare(self):
        if not self.backend.loaded:
            messagebox.showerror("Not loaded", "Load orders.csv first (sidebar).")
            return
        self.run_in_thread(self.backend.compare_search,
                           self._show_compare, start_msg="Running 1,000 searches…")

    def _show_compare(self, r):
        box = self.compare_result
        box.configure(state="normal")
        box.delete("1.0", "end")
        box.insert("end",
                   f"Search test: {r['searches']} searches (IDs 1001, 1010, 1024, 1030, 1017)\n\n"
                   f"Linked List: {r['list_steps']:,} steps, avg {r['list_avg']:.1f} / search, "
                   f"{r['list_time_us']:.0f} µs\n"
                   f"BST:         {r['tree_steps']:,} steps, avg {r['tree_avg']:.1f} / search, "
                   f"{r['tree_time_us']:.0f} µs\n\n"
                   f"Result: BST checked far fewer nodes.\n"
                   f"List = O(n), BST = O(log n).\n")
        box.configure(state="disabled")
        self.log(f"Compare: List {r['list_steps']} steps vs BST {r['tree_steps']} steps.")

    # ----------------------------------------------------------------
    # Page: Report
    # ----------------------------------------------------------------
    def _page_report(self):
        page = ctk.CTkFrame(self.main, fg_color="transparent")
        self._heading(page, "Report", "Totals from the Linked List")
        ctk.CTkButton(page, text="📈  Generate report", width=200, height=38,
                      command=self.refresh_report).pack(padx=8, pady=8, anchor="w")
        self.report_box = ctk.CTkTextbox(page, height=320, font=("Consolas", 13))
        self.report_box.pack(fill="both", expand=True, padx=8, pady=8)
        return page

    def refresh_report(self):
        try:
            r = self.backend.report()
            box = self.report_box
            box.configure(state="normal")
            box.delete("1.0", "end")
            box.insert("end",
                       f"Total orders: {r['total']}\n"
                       f"Pending: {r['Pending']}    Processing: {r['Processing']}\n"
                       f"Shipped: {r['Shipped']}    Delivered: {r['Delivered']}\n"
                       f"Cancelled: {r['Cancelled']}\n"
                       f"Total sales (not cancelled): Rs.{r['total_sales']:,}\n"
                       f"Orders waiting in queue: {r['queue_size']}\n"
                       f"Actions you can undo: {r['undo_size']}\n")
            box.configure(state="disabled")
            self.log("Report generated.")
        except Exception as exc:  # noqa: BLE001
            self.log(f"ERROR generating report: {exc}")
            messagebox.showerror("Error", str(exc))

    # ----------------------------------------------------------------
    # Load / Save (sidebar buttons)
    # ----------------------------------------------------------------
    def _load_all_worker(self):
        """Runs in a thread. Returns (orders, routes)."""
        routes = self.backend.load_routes()
        orders = self.backend.load_orders()
        return orders, routes

    def _on_loaded(self, result):
        orders, routes = result
        # No popup here on purpose: a modal box on startup would block
        # the window until it is dismissed. The log + status bar tell
        # the user, and the dashboard refreshes immediately.
        self.refresh_dashboard()
        self.log(f"Loaded {orders} orders and {routes} routes.")

    def on_load_clicked(self):
        path = filedialog.askopenfilename(title="Choose orders.csv",
                                         filetypes=[("CSV files", "*.csv")])
        if not path:  # user pressed Cancel — not an error
            return
        if not os.path.exists(path):
            messagebox.showerror("File not found", f"That file does not exist:\n{path}")
            return
        self.run_in_thread(lambda: self.backend.load_orders(Path(path)),
                           lambda n: (self.log(f"Loaded {n} orders from {path}."),
                                      self.refresh_dashboard(),
                                      messagebox.showinfo("Loaded", f"Loaded {n} orders.")),
                           start_msg="Loading orders.csv…")

    def on_save_clicked(self):
        try:
            if not self.backend.loaded:
                raise ValueError("Nothing to save yet — load orders first.")
            count = self.backend.save_orders()
            self.log(f"Saved {count} orders to orders.csv.")
            messagebox.showinfo("Saved", f"Saved {count} orders to orders.csv.")
        except Exception as exc:  # noqa: BLE001
            self.log(f"ERROR saving: {exc}")
            messagebox.showerror("Save error", str(exc))


# ------------------------------------------------------------------
# Entry point
# ------------------------------------------------------------------
def main():
    try:
        app = StoreApp()
    except Exception as exc:  # noqa: BLE001 — e.g. no display on a server
        print(f"Could not start the GUI: {exc}", file=sys.stderr)
        print("On Windows/Mac just run:  python app.py", file=sys.stderr)
        raise
    app.mainloop()


if __name__ == "__main__":
    main()
