import { Routes, Route, NavLink } from 'react-router-dom';
import TaskList from './components/TaskList';
import PlanView from './components/PlanView';
import SummaryView from './components/SummaryView';

export default function App() {
  return (
    <div className="app">
      <nav className="sidebar">
        <h1 className="logo">AutoPlanner</h1>
        <NavLink to="/" end>Tasks</NavLink>
        <NavLink to="/plan/daily">Daily Plan</NavLink>
        <NavLink to="/plan/weekly">Weekly Plan</NavLink>
        <NavLink to="/summary">Summary</NavLink>
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
