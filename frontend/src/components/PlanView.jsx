import { useState, useEffect } from 'react';
import { useParams } from 'react-router-dom';
import { getPlan, savePlan, generateWeeklyPlan, generateDailyPlans, getTasks } from '../api';

function todayStr() {
  return new Date().toISOString().slice(0, 10);
}

function getMondayOfWeek(dateStr) {
  const d = new Date(dateStr + 'T00:00:00');
  const day = d.getDay();
  const diff = (day + 6) % 7;
  d.setDate(d.getDate() - diff);
  return d.toISOString().slice(0, 10);
}

function getWeekDays(monday) {
  const days = [];
  const d = new Date(monday + 'T00:00:00');
  for (let i = 0; i < 5; i++) {
    days.push(d.toISOString().slice(0, 10));
    d.setDate(d.getDate() + 1);
  }
  return days;
}

const DAY_NAMES = ['Monday', 'Tuesday', 'Wednesday', 'Thursday', 'Friday'];

export default function PlanView() {
  const { type } = useParams();
  const [date, setDate] = useState(todayStr());
  const [plan, setPlan] = useState(null);
  const [dailyPlans, setDailyPlans] = useState([]);
  const [taskMap, setTaskMap] = useState({});
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');

  const monday = getMondayOfWeek(date);

  const load = async () => {
    setLoading(true);
    setError('');
    try {
      const tasks = await getTasks();
      const map = {};
      for (const t of tasks) map[t.id] = t;
      setTaskMap(map);

      if (type === 'weekly') {
        const planData = await getPlan('weekly', date);
        setPlan(planData && planData.id ? planData : null);
        // Also load daily plans for this week
        const weekDays = getWeekDays(monday);
        const dailies = await Promise.all(weekDays.map(d => getPlan('daily', d)));
        setDailyPlans(dailies.map((dp, i) => ({
          date: weekDays[i],
          plan: dp && dp.id ? dp : null,
        })));
      } else {
        const planData = await getPlan('daily', date);
        setPlan(planData && planData.id ? planData : null);
        setDailyPlans([]);
      }
    } catch (e) {
      setError(e.message);
    }
    setLoading(false);
  };

  useEffect(() => { load(); }, [type, date]);

  const handleGenerateWeekly = async () => {
    setLoading(true);
    try {
      await generateWeeklyPlan(date);
      load();
    } catch (e) {
      setError(e.message);
    }
    setLoading(false);
  };

  const handleGenerateDaily = async () => {
    setLoading(true);
    try {
      await generateDailyPlans(date);
      load();
    } catch (e) {
      setError(e.message);
    }
    setLoading(false);
  };

  const handleRemoveItem = async (index) => {
    if (!plan) return;
    const newItems = plan.items.filter((_, i) => i !== index);
    const updated = { ...plan, items: newItems };
    try {
      await savePlan(updated);
      load();
    } catch (e) {
      setError(e.message);
    }
  };

  const handleMoveItem = async (index, direction) => {
    if (!plan) return;
    const items = [...plan.items];
    const target = index + direction;
    if (target < 0 || target >= items.length) return;
    [items[index], items[target]] = [items[target], items[index]];
    try {
      await savePlan({ ...plan, items });
      load();
    } catch (e) {
      setError(e.message);
    }
  };

  const handleDurationChange = async (index, newDuration) => {
    if (!plan) return;
    const items = [...plan.items];
    items[index] = { ...items[index], duration_minutes: Number(newDuration) };
    try {
      await savePlan({ ...plan, items });
      load();
    } catch (e) {
      setError(e.message);
    }
  };

  if (type === 'weekly') {
    const totalMinutes = plan ? plan.items.reduce((sum, it) => sum + it.duration_minutes, 0) : 0;
    const totalHours = (totalMinutes / 60).toFixed(1);

    return (
      <div>
        <div className="page-header">
          <h2>Weekly Plan</h2>
          <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
            <input type="date" value={date} onChange={e => setDate(e.target.value)} />
            <button className="btn btn-primary" onClick={handleGenerateWeekly} disabled={loading}>
              Generate Weekly
            </button>
          </div>
        </div>
        <p style={{ color: '#636e72', marginBottom: 16 }}>
          Week of {monday} &mdash; {plan ? `${plan.items.length} tasks, ${totalHours}h total` : 'No plan yet'}
        </p>

        {error && <div style={{ color: '#d63031', marginBottom: 12 }}>{error}</div>}

        {loading ? (
          <div className="empty">Loading...</div>
        ) : !plan ? (
          <div className="empty">
            No weekly plan yet. Click "Generate Weekly" to create one from your tasks.
          </div>
        ) : (
          <>
            <div className="card">
              <table className="task-table">
                <thead>
                  <tr>
                    <th>Order</th>
                    <th></th>
                    <th>Task</th>
                    <th>Category</th>
                    <th>Priority</th>
                    <th>Duration</th>
                    <th></th>
                  </tr>
                </thead>
                <tbody>
                  {plan.items.map((item, i) => {
                    const task = taskMap[item.task_id];
                    return (
                      <tr key={i}>
                        <td>
                          <div style={{ display: 'flex', gap: 2 }}>
                            <button className="btn btn-sm" onClick={() => handleMoveItem(i, -1)} disabled={i === 0}>{'\u25B2'}</button>
                            <button className="btn btn-sm" onClick={() => handleMoveItem(i, 1)} disabled={i === plan.items.length - 1}>{'\u25BC'}</button>
                          </div>
                        </td>
                        <td><span className={`priority priority-${task?.priority || 3}`} /></td>
                        <td>{task ? task.title : `Task #${item.task_id}`}</td>
                        <td>{task?.category || '-'}</td>
                        <td>{task ? ['', 'Critical', 'High', 'Medium', 'Low', 'Minimal'][task.priority] : '-'}</td>
                        <td>
                          <input
                            type="number" min="5" step="5"
                            value={item.duration_minutes}
                            onChange={e => handleDurationChange(i, e.target.value)}
                            style={{ width: 70 }}
                          />
                          <span style={{ marginLeft: 4, color: '#636e72', fontSize: '0.8rem' }}>min</span>
                        </td>
                        <td>
                          <button className="btn btn-sm btn-danger" onClick={() => handleRemoveItem(i)}>Remove</button>
                        </td>
                      </tr>
                    );
                  })}
                </tbody>
              </table>
            </div>

            <div style={{ margin: '24px 0' }}>
              <button className="btn btn-primary" onClick={handleGenerateDaily} disabled={loading}>
                Generate Daily Plans from Weekly
              </button>
            </div>

            {dailyPlans.some(dp => dp.plan) && (
              <div>
                <h3 style={{ marginBottom: 16 }}>Daily Plans</h3>
                <div className="daily-plans-grid">
                  {dailyPlans.map((dp, i) => (
                    <div key={dp.date} className="card" style={{ marginBottom: 12 }}>
                      <h4 style={{ marginBottom: 10 }}>{DAY_NAMES[i]} &mdash; {dp.date}</h4>
                      {!dp.plan || dp.plan.items.length === 0 ? (
                        <div style={{ color: '#b2bec3', fontSize: '0.85rem' }}>No tasks</div>
                      ) : (
                        <div className="plan-timeline">
                          {dp.plan.items.map((item, j) => {
                            const task = taskMap[item.task_id];
                            return (
                              <div key={j} className="plan-slot">
                                <span className="time">{item.scheduled_time}</span>
                                <div style={{ flex: 1 }}>
                                  <strong>{task ? task.title : `Task #${item.task_id}`}</strong>
                                </div>
                                <span className="duration">{item.duration_minutes} min</span>
                              </div>
                            );
                          })}
                        </div>
                      )}
                    </div>
                  ))}
                </div>
              </div>
            )}
          </>
        )}
      </div>
    );
  }

  // ── Daily Plan View ────────────────────────────────────────────────
  return (
    <div>
      <div className="page-header">
        <h2>Daily Plan</h2>
        <input type="date" value={date} onChange={e => setDate(e.target.value)} />
      </div>

      {error && <div style={{ color: '#d63031', marginBottom: 12 }}>{error}</div>}

      {loading ? (
        <div className="empty">Loading...</div>
      ) : !plan ? (
        <div className="empty">
          No daily plan for this date. Generate it from the Weekly Plan page.
        </div>
      ) : plan.items.length === 0 ? (
        <div className="empty">No tasks scheduled for this day.</div>
      ) : (
        <div className="plan-timeline">
          {plan.items.map((item, i) => {
            const task = taskMap[item.task_id];
            return (
              <div key={i} className="plan-slot">
                <span className="time">{item.scheduled_time}</span>
                <div style={{ flex: 1 }}>
                  <strong>{task ? task.title : `Task #${item.task_id}`}</strong>
                  {task && <div style={{ fontSize: '0.8rem', color: '#636e72' }}>{task.category}</div>}
                </div>
                <span className="duration">{item.duration_minutes} min</span>
                {task && <span className={`priority priority-${task.priority}`} />}
              </div>
            );
          })}
        </div>
      )}
    </div>
  );
}
