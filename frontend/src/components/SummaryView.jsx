import { useState, useEffect } from 'react';
import { getSummary, generateSummary, getAllSummaries } from '../api';
import { formatDuration } from '../helpers';

function todayStr() {
  return new Date().toISOString().slice(0, 10);
}

function getMondayOfWeek(dateStr) {
  const d = new Date(dateStr + 'T00:00:00');
  const diff = (d.getDay() + 6) % 7;
  d.setDate(d.getDate() - diff);
  return d.toISOString().slice(0, 10);
}

function CompletionBar({ completed, total }) {
  const pct = total > 0 ? Math.round((completed / total) * 100) : 0;
  return (
    <div className="completion-bar-track">
      <div className="completion-bar-fill" style={{ width: `${pct}%` }} />
      <span className="completion-bar-label">{pct}%</span>
    </div>
  );
}

function groupByRoot(tasks) {
  const groups = {};
  const order = [];
  for (const t of tasks) {
    const rootId = t.root_task_id || 0;
    const rootTitle = t.root_task_title || t.title;
    if (!groups[rootId]) {
      groups[rootId] = { rootId, rootTitle, items: [] };
      order.push(rootId);
    }
    groups[rootId].items.push(t);
  }
  return order.map(id => groups[id]);
}

function RootTaskBreakdown({ summary }) {
  const allTasks = [...summary.completed_tasks, ...summary.incomplete_tasks];
  if (allTasks.length === 0) return null;

  const groups = groupByRoot(allTasks);

  return (
    <div style={{ marginTop: 20 }}>
      <h4 style={{ marginBottom: 12 }}>By Project</h4>
      {groups.map(group => {
        const completed = group.items.filter(t => t.actual_minutes !== undefined);
        const incomplete = group.items.filter(t => t.actual_minutes === undefined);
        const totalPlanned = group.items.reduce((s, t) => s + t.planned_minutes, 0);
        const totalCompleted = completed.reduce((s, t) => s + t.planned_minutes, 0);

        return (
          <div key={group.rootId} className="plan-group" style={{ marginBottom: 16 }}>
            <div className="plan-group-header">
              <strong>{group.rootTitle}</strong>
              <span style={{ color: '#636e72', fontSize: '0.85rem', marginLeft: 8 }}>
                {completed.length}/{group.items.length} tasks
              </span>
              <span style={{ marginLeft: 'auto' }}>
                <CompletionBar completed={totalCompleted} total={totalPlanned} />
              </span>
              <span style={{ color: '#636e72', fontSize: '0.8rem', marginLeft: 12, minWidth: 80, textAlign: 'right' }}>
                {formatDuration(totalCompleted)}/{formatDuration(totalPlanned)}
              </span>
            </div>
            <div className="plan-group-items">
              {group.items.map((t, i) => {
                const isDone = t.actual_minutes !== undefined;
                return (
                  <div key={i} className={`plan-group-item ${isDone ? 'plan-item-done' : ''}`}>
                    <span className={`status-badge ${isDone ? 'status-done' : `status-${t.status || 'todo'}`}`} style={{ marginRight: 8 }}>
                      {isDone ? 'done' : (t.status || 'todo').replace('_', ' ')}
                    </span>
                    <div style={{ flex: 1 }}>
                      <span style={isDone ? { textDecoration: 'line-through', color: '#b2bec3' } : {}}>
                        {t.title}
                      </span>
                      <span style={{ color: '#636e72', fontSize: '0.8rem', marginLeft: 8 }}>{t.category}</span>
                    </div>
                    <span style={{ color: '#636e72', fontSize: '0.8rem' }}>
                      {isDone
                        ? `${formatDuration(t.actual_minutes || t.planned_minutes)} actual / ${formatDuration(t.planned_minutes)} planned`
                        : formatDuration(t.planned_minutes)
                      }
                    </span>
                  </div>
                );
              })}
            </div>
          </div>
        );
      })}
    </div>
  );
}

function SummaryCard({ summary }) {
  const completionRate = summary.tasks_planned > 0
    ? Math.round((summary.tasks_completed / summary.tasks_planned) * 100) : 0;
  const efficiency = summary.total_completed > 0 && summary.total_actual > 0
    ? Math.round((summary.total_completed / summary.total_actual) * 100) : 0;

  return (
    <div className="card" style={{ marginBottom: 20 }}>
      <div style={{ display: 'flex', justifyContent: 'space-between', alignItems: 'center', marginBottom: 16 }}>
        <h3>Week of {summary.week_date}</h3>
        <span style={{ color: '#636e72', fontSize: '0.85rem' }}>
          Generated {summary.created_at}
        </span>
      </div>

      {/* Stats row */}
      <div className="summary-stats">
        <div className="stat-card">
          <div className="stat-value">{summary.tasks_completed}/{summary.tasks_planned}</div>
          <div className="stat-label">Tasks Completed</div>
          <CompletionBar completed={summary.tasks_completed} total={summary.tasks_planned} />
        </div>
        <div className="stat-card">
          <div className="stat-value">{formatDuration(summary.total_completed)}</div>
          <div className="stat-label">Work Completed</div>
          <div style={{ fontSize: '0.8rem', color: '#636e72' }}>
            of {formatDuration(summary.total_planned)} planned
          </div>
        </div>
        <div className="stat-card">
          <div className="stat-value">{formatDuration(summary.total_actual)}</div>
          <div className="stat-label">Actual Time Spent</div>
          <div style={{ fontSize: '0.8rem', color: '#636e72' }}>
            {efficiency}% efficiency
          </div>
        </div>
        <div className="stat-card">
          <div className="stat-value">{summary.tasks_carried_over}</div>
          <div className="stat-label">Carried Over</div>
        </div>
      </div>

      {/* Category breakdown */}
      {Object.keys(summary.category_breakdown).length > 0 && (
        <div style={{ marginTop: 20 }}>
          <h4 style={{ marginBottom: 10 }}>By Category</h4>
          <div className="category-bars">
            {Object.entries(summary.category_breakdown).map(([cat, data]) => (
              <div key={cat} className="category-bar-row">
                <span className="category-bar-label">{cat}</span>
                <CompletionBar completed={data.completed} total={data.planned} />
                <span style={{ fontSize: '0.8rem', color: '#636e72', minWidth: 80, textAlign: 'right' }}>
                  {formatDuration(data.completed)}/{formatDuration(data.planned)}
                </span>
              </div>
            ))}
          </div>
        </div>
      )}

      {/* Tasks grouped by root task */}
      <RootTaskBreakdown summary={summary} />
    </div>
  );
}

export default function SummaryView() {
  const [date, setDate] = useState(todayStr());
  const [summary, setSummary] = useState(null);
  const [history, setHistory] = useState([]);
  const [loading, setLoading] = useState(false);
  const [error, setError] = useState('');
  const [showHistory, setShowHistory] = useState(false);

  const load = async () => {
    setLoading(true);
    setError('');
    try {
      const s = await getSummary(date);
      setSummary(s && s.id ? s : null);
      const all = await getAllSummaries();
      setHistory(Array.isArray(all) ? all : []);
    } catch (e) {
      setError(e.message);
    }
    setLoading(false);
  };

  useEffect(() => { load(); }, [date]);

  const handleGenerate = async () => {
    setLoading(true);
    try {
      await generateSummary(date);
      load();
    } catch (e) {
      setError(e.message);
    }
    setLoading(false);
  };

  return (
    <div>
      <div className="page-header">
        <h2>Weekly Summary</h2>
        <div style={{ display: 'flex', gap: 10, alignItems: 'center' }}>
          <input type="date" value={date} onChange={e => setDate(e.target.value)} />
          <button className="btn btn-primary" onClick={handleGenerate} disabled={loading}>
            Generate Summary
          </button>
        </div>
      </div>

      {error && <div style={{ color: '#d63031', marginBottom: 12 }}>{error}</div>}

      {loading ? (
        <div className="empty">Loading...</div>
      ) : !summary ? (
        <div className="empty">
          No summary for this week yet. Click "Generate Summary" to create one.
        </div>
      ) : (
        <SummaryCard summary={summary} />
      )}

      {/* History */}
      {history.length > 0 && (
        <div style={{ marginTop: 24 }}>
          <h3
            style={{ cursor: 'pointer', display: 'flex', alignItems: 'center', gap: 8 }}
            onClick={() => setShowHistory(!showHistory)}
          >
            <span>{showHistory ? '\u25BE' : '\u25B8'}</span>
            Past Summaries ({history.length})
          </h3>
          {showHistory && (
            <div style={{ marginTop: 12 }}>
              {history.filter(h => h.week_date !== getMondayOfWeek(date)).map(h => (
                <SummaryCard key={h.id} summary={h} />
              ))}
            </div>
          )}
        </div>
      )}
    </div>
  );
}
