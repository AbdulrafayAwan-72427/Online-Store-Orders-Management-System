import { useEffect, useState } from 'react';
import { NavLink, Outlet } from 'react-router-dom';
import { GraduationCap, LayoutDashboard, ListOrdered, Menu, Moon, Package, Route, ScrollText, Sun, Truck, X } from 'lucide-react';
import { ApiError, getHealth, type Health } from '../lib/api';

const NAV = [
  { to: '/', label: 'Dashboard', icon: LayoutDashboard, end: true },
  { to: '/orders', label: 'Orders', icon: Package, end: false },
  { to: '/planner', label: 'Delivery Planner', icon: Route, end: false },
  { to: '/trips', label: 'Delivery Trips', icon: Truck, end: false },
  { to: '/logs', label: 'Activity Logs', icon: ScrollText, end: false },
  { to: '/queue', label: 'Processing Queue', icon: ListOrdered, end: false },
  { to: '/dsa', label: 'DSA Performance', icon: GraduationCap, end: false },
];

function HealthPill() {
  const [health, setHealth] = useState<Health | null>(null);
  const [error, setError] = useState<string | null>(null);

  useEffect(() => {
    let alive = true;
    const check = () => {
      getHealth()
        .then((h) => {
          if (alive) {
            setHealth(h);
            setError(null);
          }
        })
        .catch((e: unknown) => {
          if (alive) {
            setHealth(null);
            setError(e instanceof ApiError ? e.message : 'Backend unreachable');
          }
        });
    };
    check();
    const t = window.setInterval(check, 15000);
    return () => {
      alive = false;
      window.clearInterval(t);
    };
  }, []);

  if (error) {
    return (
      <span className="inline-flex items-center gap-2 rounded-full bg-red-100 px-3 py-1 text-xs font-semibold text-red-800 dark:bg-red-900/50 dark:text-red-200" title={error}>
        <span className="h-2 w-2 rounded-full bg-red-600" aria-hidden />
        API offline
      </span>
    );
  }
  return (
    <span className="inline-flex items-center gap-2 rounded-full bg-emerald-100 px-3 py-1 text-xs font-semibold text-emerald-800 dark:bg-emerald-900/50 dark:text-emerald-200" title={health ? `${health.orders} orders loaded` : ''}>
      <span className="h-2 w-2 rounded-full bg-emerald-600" aria-hidden />
      API connected
    </span>
  );
}

export default function Layout() {
  const [sidebarOpen, setSidebarOpen] = useState(false);
  const [collapsed, setCollapsed] = useState(false);
  const [dark, setDark] = useState(() => {
    if (typeof window === 'undefined') return false;
    return window.localStorage.getItem('oso-theme') === 'dark';
  });

  useEffect(() => {
    document.documentElement.classList.toggle('dark', dark);
    window.localStorage.setItem('oso-theme', dark ? 'dark' : 'light');
  }, [dark]);

  const sidebarContent = (
    <div className="flex h-full flex-col">
      <div className="flex h-16 items-center gap-2 border-b border-slate-200 px-4 dark:border-slate-700">
        <Package className="h-6 w-6 shrink-0 text-emerald-600" aria-hidden />
        {!collapsed && (
          <div className="min-w-0">
            <p className="truncate text-sm font-bold text-slate-900 dark:text-white">Online Store Orders</p>
            <p className="truncate text-xs text-slate-500 dark:text-slate-400">Admin · C++ API</p>
          </div>
        )}
      </div>
      <nav className="flex-1 space-y-1 p-3" aria-label="Main navigation">
        {NAV.map(({ to, label, icon: Icon, end }) => (
          <NavLink
            key={to}
            to={to}
            end={end}
            onClick={() => setSidebarOpen(false)}
            className={({ isActive }) =>
              `flex items-center gap-3 rounded-lg px-3 py-2 text-sm font-medium ${
                isActive
                  ? 'bg-emerald-100 text-emerald-900 dark:bg-emerald-900/50 dark:text-emerald-100'
                  : 'text-slate-600 hover:bg-slate-100 dark:text-slate-300 dark:hover:bg-slate-800'
              }`
            }
            title={label}
          >
            <Icon className="h-5 w-5 shrink-0" aria-hidden />
            {!collapsed && <span>{label}</span>}
          </NavLink>
        ))}
      </nav>
      <div className="border-t border-slate-200 p-3 text-xs text-slate-500 dark:border-slate-700 dark:text-slate-400">
        {!collapsed && <p>Backend: C++17 · cpp-httplib · nlohmann/json</p>}
        {!collapsed && <p className="mt-1">All data is live from the API — nothing is hardcoded.</p>}
      </div>
    </div>
  );

  return (
    <div className="flex h-screen overflow-hidden bg-slate-100 text-slate-900 dark:bg-slate-950 dark:text-slate-100">
      {/* Desktop sidebar */}
      <aside className={`hidden border-r border-slate-200 bg-white md:block dark:border-slate-700 dark:bg-slate-900 ${collapsed ? 'w-16' : 'w-64'} transition-all`}>
        {sidebarContent}
      </aside>
      {/* Mobile sidebar overlay */}
      {sidebarOpen && (
        <div className="fixed inset-0 z-40 md:hidden">
          <div className="absolute inset-0 bg-black/50" onClick={() => setSidebarOpen(false)} aria-hidden />
          <aside className="absolute inset-y-0 left-0 w-64 bg-white shadow-xl dark:bg-slate-900">
            <button onClick={() => setSidebarOpen(false)} aria-label="Close menu" className="absolute right-2 top-4 rounded-md p-1 text-slate-500 hover:bg-slate-100 dark:text-slate-300">
              <X className="h-5 w-5" aria-hidden />
            </button>
            {sidebarContent}
          </aside>
        </div>
      )}

      <div className="flex min-w-0 flex-1 flex-col">
        <header className="flex h-16 shrink-0 items-center gap-3 border-b border-slate-200 bg-white px-4 dark:border-slate-700 dark:bg-slate-900">
          <button onClick={() => setSidebarOpen(true)} aria-label="Open menu" className="rounded-md p-2 text-slate-600 hover:bg-slate-100 md:hidden dark:text-slate-300 dark:hover:bg-slate-800">
            <Menu className="h-5 w-5" aria-hidden />
          </button>
          <button
            onClick={() => setCollapsed((c) => !c)}
            aria-label={collapsed ? 'Expand sidebar' : 'Collapse sidebar'}
            className="hidden rounded-md p-2 text-slate-600 hover:bg-slate-100 md:block dark:text-slate-300 dark:hover:bg-slate-800"
          >
            <Menu className="h-5 w-5" aria-hidden />
          </button>
          <h1 className="min-w-0 flex-1 truncate text-lg font-semibold">Order &amp; Delivery Management</h1>
          <HealthPill />
          <button
            onClick={() => setDark((d) => !d)}
            aria-label={dark ? 'Switch to light mode' : 'Switch to dark mode'}
            className="rounded-md p-2 text-slate-600 hover:bg-slate-100 dark:text-slate-300 dark:hover:bg-slate-800"
          >
            {dark ? <Sun className="h-5 w-5" aria-hidden /> : <Moon className="h-5 w-5" aria-hidden />}
          </button>
        </header>
        <main className="flex-1 overflow-y-auto p-4 sm:p-6">
          <Outlet />
        </main>
      </div>
    </div>
  );
}
