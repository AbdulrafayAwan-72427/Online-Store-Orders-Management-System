import { Suspense, lazy } from 'react';
import { BrowserRouter, Route, Routes } from 'react-router-dom';
import Layout from './components/Layout';
import { Spinner } from './components/ui';

// Route-based code splitting: each page is its own chunk, loaded on demand.
const Dashboard = lazy(() => import('./pages/Dashboard'));
const Orders = lazy(() => import('./pages/Orders'));
const DeliveryPlanner = lazy(() => import('./pages/DeliveryPlanner'));
const Trips = lazy(() => import('./pages/Trips'));
const ActivityLogs = lazy(() => import('./pages/ActivityLogs'));
const Queue = lazy(() => import('./pages/Queue'));
const DSA = lazy(() => import('./pages/DSA'));

export default function App() {
  return (
    <BrowserRouter>
      <Suspense fallback={<Spinner label="Loading page…" />}>
        <Routes>
          <Route element={<Layout />}>
            <Route index element={<Dashboard />} />
            <Route path="orders" element={<Orders />} />
            <Route path="planner" element={<DeliveryPlanner />} />
            <Route path="trips" element={<Trips />} />
            <Route path="logs" element={<ActivityLogs />} />
            <Route path="queue" element={<Queue />} />
            <Route path="dsa" element={<DSA />} />
            <Route path="*" element={<Dashboard />} />
          </Route>
        </Routes>
      </Suspense>
    </BrowserRouter>
  );
}
