// 1 day = 8 working hours = 480 minutes
const MINUTES_PER_HOUR = 60;
const MINUTES_PER_DAY = 480;

export function toMinutes(value, unit) {
  const v = Number(value);
  if (unit === 'hours') return Math.round(v * MINUTES_PER_HOUR);
  if (unit === 'days') return Math.round(v * MINUTES_PER_DAY);
  return v; // minutes
}

export function fromMinutes(minutes, unit) {
  if (unit === 'hours') return +(minutes / MINUTES_PER_HOUR).toFixed(1);
  if (unit === 'days') return +(minutes / MINUTES_PER_DAY).toFixed(1);
  return minutes;
}

export function formatDuration(minutes) {
  if (minutes >= MINUTES_PER_DAY && minutes % MINUTES_PER_DAY === 0) {
    const d = minutes / MINUTES_PER_DAY;
    return `${d}d`;
  }
  if (minutes >= MINUTES_PER_HOUR && minutes % MINUTES_PER_HOUR === 0) {
    const h = minutes / MINUTES_PER_HOUR;
    return `${h}h`;
  }
  if (minutes >= MINUTES_PER_HOUR) {
    const h = Math.floor(minutes / MINUTES_PER_HOUR);
    const m = minutes % MINUTES_PER_HOUR;
    return m > 0 ? `${h}h${m}m` : `${h}h`;
  }
  return `${minutes}m`;
}

// Best unit to display a given minutes value
export function bestUnit(minutes) {
  if (minutes >= MINUTES_PER_DAY && minutes % MINUTES_PER_DAY === 0) return 'days';
  if (minutes >= MINUTES_PER_HOUR) return 'hours';
  return 'minutes';
}

// Build "Parent > Child > Grandchild" path for a task
export function getTaskPath(taskId, taskMap) {
  const parts = [];
  let current = taskMap[taskId];
  while (current) {
    parts.unshift(current.title);
    current = current.parent_id ? taskMap[current.parent_id] : null;
  }
  return parts.join(' > ');
}
