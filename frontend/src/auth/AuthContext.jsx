import { createContext, useContext, useState, useEffect, useCallback } from 'react';
import { useMsal } from '@azure/msal-react';
import { loginRequest } from './authConfig';

const AuthContext = createContext(null);

export function AuthProvider({ children }) {
  const { instance } = useMsal();
  const [user, setUser] = useState(null);
  const [loading, setLoading] = useState(true);

  useEffect(() => {
    const token = localStorage.getItem('session_token');
    if (token) {
      fetch('/api/auth/me', {
        headers: { Authorization: `Bearer ${token}` },
      })
        .then((res) => (res.ok ? res.json() : Promise.reject()))
        .then((data) => setUser(data))
        .catch(() => localStorage.removeItem('session_token'))
        .finally(() => setLoading(false));
    } else {
      setLoading(false);
    }
  }, []);

  const login = useCallback(async () => {
    try {
      const result = await instance.loginPopup(loginRequest);
      // Send both tokens: idToken (JWT, decodable) for user info,
      // accessToken for MS Graph verification when SSL is available
      const res = await fetch('/api/auth/login', {
        method: 'POST',
        headers: { 'Content-Type': 'application/json' },
        body: JSON.stringify({
          provider: 'microsoft',
          token: result.accessToken,
          id_token: result.idToken,
        }),
      });

      if (!res.ok) {
        const err = await res.json().catch(() => ({}));
        throw new Error(err.error || 'Login failed');
      }

      const data = await res.json();
      localStorage.setItem('session_token', data.session_token);
      setUser(data.user);
    } catch (err) {
      console.error('Login error:', err);
      throw err;
    }
  }, [instance]);

  const logout = useCallback(async () => {
    const token = localStorage.getItem('session_token');
    if (token) {
      await fetch('/api/auth/logout', {
        method: 'POST',
        headers: { Authorization: `Bearer ${token}` },
      }).catch(() => {});
    }
    localStorage.removeItem('session_token');
    setUser(null);
  }, []);

  return (
    <AuthContext.Provider value={{ user, loading, login, logout }}>
      {children}
    </AuthContext.Provider>
  );
}

export const useAuth = () => useContext(AuthContext);
