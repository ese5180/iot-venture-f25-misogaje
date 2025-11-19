import {
  CognitoUserPool,
  CognitoUser,
  CognitoUserSession,
  CognitoIdToken,
  CognitoAccessToken,
  CognitoRefreshToken,
} from 'amazon-cognito-identity-js';
import {
  CognitoIdentityProviderClient,
  SignUpCommand,
  ConfirmSignUpCommand,
  InitiateAuthCommand,
  AuthFlowType,
} from '@aws-sdk/client-cognito-identity-provider';
import * as crypto from 'crypto';

const poolData = {
  UserPoolId: process.env.NEXT_PUBLIC_COGNITO_USER_POOL_ID!,
  ClientId: process.env.NEXT_PUBLIC_COGNITO_CLIENT_ID!,
};

const userPool = new CognitoUserPool(poolData);

const clientSecret = process.env.NEXT_PUBLIC_COGNITO_CLIENT_SECRET;
const region = process.env.NEXT_PUBLIC_AWS_REGION || 'us-east-2';

const cognitoClient = new CognitoIdentityProviderClient({ region });

// Calculate SECRET_HASH for Cognito authentication
const calculateSecretHash = (username: string): string => {
  if (!clientSecret) {
    throw new Error('Client secret is not configured');
  }

  const message = username + poolData.ClientId;
  const hmac = crypto.createHmac('sha256', clientSecret);
  hmac.update(message);
  return hmac.digest('base64');
};

export interface SignUpParams {
  username: string;
  password: string;
  email: string;
}

export interface SignInParams {
  username: string;
  password: string;
}

export interface ConfirmSignUpParams {
  username: string;
  code: string;
}

export const signUp = async (params: SignUpParams): Promise<any> => {
  try {
    const secretHash = calculateSecretHash(params.username);

    // Extract nickname from email (part before @)
    const nickName = params.email.split('@')[0];

    const command = new SignUpCommand({
      ClientId: poolData.ClientId,
      Username: params.username,
      Password: params.password,
      SecretHash: secretHash,
      UserAttributes: [
        {
          Name: 'email',
          Value: params.email,
        },
        {
          Name: 'nickname',
          Value: nickName,
        },
      ],
    });

    const response = await cognitoClient.send(command);
    return response;
  } catch (error) {
    throw error;
  }
};

export const confirmSignUp = async (params: ConfirmSignUpParams): Promise<any> => {
  try {
    const secretHash = calculateSecretHash(params.username);

    const command = new ConfirmSignUpCommand({
      ClientId: poolData.ClientId,
      Username: params.username,
      ConfirmationCode: params.code,
      SecretHash: secretHash,
    });

    const response = await cognitoClient.send(command);
    return response;
  } catch (error) {
    throw error;
  }
};

export const signIn = async (params: SignInParams): Promise<any> => {
  try {
    const secretHash = calculateSecretHash(params.username);

    const command = new InitiateAuthCommand({
      AuthFlow: AuthFlowType.USER_PASSWORD_AUTH,
      ClientId: poolData.ClientId,
      AuthParameters: {
        USERNAME: params.username,
        PASSWORD: params.password,
        SECRET_HASH: secretHash,
      },
    });

    const response = await cognitoClient.send(command);

    if (response.AuthenticationResult) {
      // Store tokens in local storage for CognitoUserPool to use
      const tokens = response.AuthenticationResult;
      const cognitoUser = new CognitoUser({
        Username: params.username,
        Pool: userPool,
      });

      // Create session from tokens
      const idToken = new CognitoIdToken({ IdToken: tokens.IdToken! });
      const accessToken = new CognitoAccessToken({ AccessToken: tokens.AccessToken! });
      const refreshToken = new CognitoRefreshToken({ RefreshToken: tokens.RefreshToken! });

      const session = new CognitoUserSession({
        IdToken: idToken,
        AccessToken: accessToken,
        RefreshToken: refreshToken,
      });

      // Store session in local storage
      cognitoUser.setSignInUserSession(session);

      return response.AuthenticationResult;
    } else {
      throw new Error('Authentication failed');
    }
  } catch (error) {
    throw error;
  }
};

export const signOut = (): Promise<void> => {
  return new Promise((resolve) => {
    const cognitoUser = userPool.getCurrentUser();
    if (cognitoUser) {
      cognitoUser.signOut();
    }
    resolve();
  });
};

export const getCurrentUser = (): Promise<any> => {
  return new Promise((resolve, reject) => {
    const cognitoUser = userPool.getCurrentUser();

    if (!cognitoUser) {
      reject(new Error('No user found'));
      return;
    }

    cognitoUser.getSession((err: any, session: any) => {
      if (err) {
        reject(err);
        return;
      }

      if (!session.isValid()) {
        reject(new Error('Session is not valid'));
        return;
      }

      cognitoUser.getUserAttributes((err, attributes) => {
        if (err) {
          reject(err);
          return;
        }

        const userData: any = {};
        attributes?.forEach((attribute) => {
          userData[attribute.Name] = attribute.Value;
        });

        resolve({
          username: cognitoUser.getUsername(),
          attributes: userData,
          session,
        });
      });
    });
  });
};

export const getSession = (): Promise<any> => {
  return new Promise((resolve, reject) => {
    const cognitoUser = userPool.getCurrentUser();

    if (!cognitoUser) {
      reject(new Error('No user found'));
      return;
    }

    cognitoUser.getSession((err: any, session: any) => {
      if (err) {
        reject(err);
        return;
      }
      resolve(session);
    });
  });
};
