const BASE = '/api';

async function request(path, options = {}) {
  const res = await fetch(`${BASE}${path}`, {
    headers: { 'Content-Type': 'application/json' },
    ...options,
  });
  if (!res.ok) {
    const err = await res.json().catch(() => ({ error: res.statusText }));
    throw new Error(err.error || 'Request failed');
  }
  return res.json();
}

// Tasks
export const getTasks = (params = {}) => {
  const query = new URLSearchParams(params).toString();
  return request(`/tasks${query ? '?' + query : ''}`);
};
export const getTask = (id) => request(`/tasks/${id}`);
export const createTask = (task) => request('/tasks', { method: 'POST', body: JSON.stringify(task) });
export const updateTask = (id, task) => request(`/tasks/${id}`, { method: 'PUT', body: JSON.stringify(task) });
export const deleteTask = (id) => request(`/tasks/${id}`, { method: 'DELETE' });

// Plans
export const getPlan = (type, date) => request(`/plans?type=${type}&date=${date}`);
export const savePlan = (plan) => request('/plans', { method: 'POST', body: JSON.stringify(plan) });
export const generateWeeklyPlan = (date) =>
  request('/plans/generate-weekly', { method: 'POST', body: JSON.stringify({ date }) });
export const generateDailyPlans = (date) =>
  request('/plans/generate-daily', { method: 'POST', body: JSON.stringify({ date }) });

// Productivity
export const getProductivityLogs = (taskId) => request(`/productivity/${taskId}`);
export const createProductivityLog = (log) =>
  request('/productivity', { method: 'POST', body: JSON.stringify(log) });

// Categories
export const getCategories = () => request('/categories');
