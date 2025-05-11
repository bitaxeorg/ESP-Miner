import preactLogo from "../../assets/preact.svg";
import { useState, useEffect, useRef } from "preact/hooks";

export function Home() {
  return (
    <div class='space-y-6'>
      <div class='rounded-lg bg-gray-900 p-6'>
        <h2 class='mb-4 text-xl font-semibold text-gray-100'>System Status</h2>
        <div class='grid gap-4 sm:grid-cols-2 lg:grid-cols-3'>
          <div class='rounded-lg bg-gray-800 p-4'>
            <h3 class='text-sm font-medium text-gray-400'>Temperature</h3>
            <p class='mt-2 text-2xl font-semibold text-gray-100'>45°C</p>
          </div>
          <div class='rounded-lg bg-gray-800 p-4'>
            <h3 class='text-sm font-medium text-gray-400'>Hashrate</h3>
            <p class='mt-2 text-2xl font-semibold text-gray-100'>45 MH/s</p>
          </div>
          <div class='rounded-lg bg-gray-800 p-4'>
            <h3 class='text-sm font-medium text-gray-400'>Uptime</h3>
            <p class='mt-2 text-2xl font-semibold text-gray-100'>24h 35m</p>
          </div>
        </div>
      </div>

      <div class='rounded-lg bg-gray-900 p-6'>
        <h2 class='mb-4 text-xl font-semibold text-gray-100'>Recent Activity</h2>
        <div class='space-y-4'>
          <div class='flex items-center justify-between rounded-lg bg-gray-800 p-4'>
            <div>
              <h3 class='text-sm font-medium text-gray-400'>Share Accepted</h3>
              <p class='mt-1 text-sm text-gray-300'>Difficulty: 2.5</p>
            </div>
            <span class='text-sm text-gray-400'>2m ago</span>
          </div>
          <div class='flex items-center justify-between rounded-lg bg-gray-800 p-4'>
            <div>
              <h3 class='text-sm font-medium text-gray-400'>New Block Found</h3>
              <p class='mt-1 text-sm text-gray-300'>Height: 123456</p>
            </div>
            <span class='text-sm text-gray-400'>15m ago</span>
          </div>
        </div>
      </div>
    </div>
  );
}

function Resource(props) {
  return (
    <a href={props.href} target='_blank' class='resource'>
      <h2>{props.title}</h2>
      <p>{props.description}</p>
    </a>
  );
}
