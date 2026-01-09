'use client';

import React, { createContext, useContext, useState, useEffect } from 'react';
import * as cognito from '../lib/cognito';

interface User {
  username: string;
  email?: string;
  attributes?: any;
}

interface AuthContextType {
  user: User | null;
  loading: boolean;
  signUp: (username: string, password: string, email: string) => Promise<any>;
  confirmSignUp: (username: string, code: string) => Promise<any>;
  signIn: (username: string, password: string) => Promise<any>;
  signOut: () => Promise<void>;
  refreshUser: () => Promise<void>;
}

const AuthContext = createContext<AuthContextType | undefined>(undefined);

export const AuthProvider: React.FC<{ children: React.ReactNode }> = ({
  children,
}) => {
  const [user, setUser] = useState<User | null>(null);
  const [loading, setLoading] = useState(true);

  const refreshUser = async () => {
    try {
      const currentUser = await cognito.getCurrentUser();
      setUser({
        username: currentUser.username,
        email: currentUser.attributes?.email,
        attributes: currentUser.attributes,
      });
    } catch (error) {
      setUser(null);
    }
  };

  useEffect(() => {
    const initAuth = async () => {
      try {
        await refreshUser();
      } catch (error) {
        console.error('Error initializing auth:', error);
      } finally {
        setLoading(false);
      }
    };

    initAuth();
  }, []);

  const handleSignUp = async (
    username: string,
    password: string,
    email: string
  ) => {
    const result = await cognito.signUp({ username, password, email });
    return result;
  };

  const handleConfirmSignUp = async (username: string, code: string) => {
    const result = await cognito.confirmSignUp({ username, code });
    return result;
  };

  const handleSignIn = async (username: string, password: string) => {
    const result = await cognito.signIn({ username, password });
    await refreshUser();
    return result;
  };

  const handleSignOut = async () => {
    await cognito.signOut();
    setUser(null);
  };

  const value = {
    user,
    loading,
    signUp: handleSignUp,
    confirmSignUp: handleConfirmSignUp,
    signIn: handleSignIn,
    signOut: handleSignOut,
    refreshUser,
  };

  return <AuthContext.Provider value={value}>{children}</AuthContext.Provider>;
};

export const useAuth = () => {
  const context = useContext(AuthContext);
  if (context === undefined) {
    throw new Error('useAuth must be used within an AuthProvider');
  }
  return context;
};
