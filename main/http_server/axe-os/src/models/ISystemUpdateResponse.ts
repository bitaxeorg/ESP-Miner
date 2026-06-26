export interface ISystemUpdateResponse {
  status: string;
  redirect?: {
    url: string;
    delay: number;
    message: string;
  };
}