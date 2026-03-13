import { Routes, Route, NavLink } from 'react-router-dom';
import { useAuth } from './auth/AuthContext';
import LoginPage from './auth/LoginPage';
import TaskList from './components/TaskList';
import PlanView from './components/PlanView';
import SummaryView from './components/SummaryView';

export default function App() {
  const { user, loading, logout } = useAuth();

  if (loading) {
    return (
      <div className="login-page">
        <div className="login-card">
          <h1 className="login-title">AutoPlanner</h1>
          <p className="login-subtitle">Loading...</p>
        </div>
      </div>
    );
  }

  if (!user) {
    return <LoginPage />;
  }

  return (
    <div className="app">
      <nav className="sidebar">
        <h1 className="logo">AutoPlanner</h1>
        <NavLink to="/" end>Tasks</NavLink>
        <NavLink to="/plan/daily">Daily Plan</NavLink>
        <NavLink to="/plan/weekly">Weekly Plan</NavLink>
        <NavLink to="/summary">Summary</NavLink>

        <div className="sidebar-user">
          <div className="sidebar-user-name">{user.name || user.email}</div>
          <div className="sidebar-user-email">{user.email}</div>
          <button className="btn-signout" onClick={logout}>Sign out</button>
        </div>
      </nav>
      <main className="content">
        <Routes>
          <Route path="/" element={<TaskList />} />
          <Route path="/plan/:type" element={<PlanView />} />
          <Route path="/summary" element={<SummaryView />} />
        </Routes>
      </main>
    </div>
  );
}
