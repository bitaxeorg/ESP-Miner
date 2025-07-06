import { useState, useEffect } from "preact/hooks";
import { Container } from "../../components/Container";
import { PageHeading } from "../../components/PageHeading";
import { ActionCard } from "../../components/ActionCard";
import { Button } from "../../components/Button";
import { fetchYouTubeStats, postYouTubeStatsToESP, getUploadsPlaylistId, fetchLatestVideos } from "../../utils/api";
import { logger } from "../../utils/logger";
import { useToast } from "../../context/ToastContext";

interface YouTubeStats {
  subscriberCount: string;
  viewCount: string;
  videoCount: string;
  title: string;
  description: string;
  thumbnailUrl: string;
}

interface LatestVideo {
  title: string;
  publishedAt: string;
  relativeTime: string;
  videoId: string;
  thumbnailUrl: string;
}

export function YouTubeStatsPage() {
  const { showToast } = useToast();
  const [channelId, setChannelId] = useState("UCT44w6854K62cSiwA1aiXfw"); // VoskCoin channel ID as default
  const [stats, setStats] = useState<YouTubeStats | null>(null);
  const [latestVideos, setLatestVideos] = useState<LatestVideo[]>([]);
  const [uploadsPlaylistId, setUploadsPlaylistId] = useState<string>("");
  const [loadingStats, setLoadingStats] = useState(false);
  const [loadingVideos, setLoadingVideos] = useState(false);
  const [postingToESP, setPostingToESP] = useState(false);
  const [autoRefreshStats, setAutoRefreshStats] = useState(false);
  const [autoRefreshVideos, setAutoRefreshVideos] = useState(false);
  const [quotaInfo, setQuotaInfo] = useState({
    statsCallsToday: 0,
    videosCallsToday: 0,
    estimatedQuotaUsed: 0
  });

  // Auto refresh stats every 90 seconds if enabled
  useEffect(() => {
    if (!autoRefreshStats || !channelId) return;

    const interval = setInterval(() => {
      fetchStatsForChannel(false); // Don't show success toast for auto-refresh
    }, 90000); // 90 seconds

    return () => clearInterval(interval);
  }, [autoRefreshStats, channelId]);

  // Auto refresh videos every 5 minutes if enabled
  useEffect(() => {
    if (!autoRefreshVideos || !uploadsPlaylistId) return;

    const interval = setInterval(() => {
      fetchVideosWithPlaylistId(uploadsPlaylistId, false); // Don't show success toast for auto-refresh
    }, 300000); // 5 minutes

    return () => clearInterval(interval);
  }, [autoRefreshVideos, uploadsPlaylistId]);

  // Initialize page with cached data and auto-fetch
  useEffect(() => {
    const now = new Date();
    const lastReset = window.localStorage.getItem('youtube_quota_reset');
    const today = now.toDateString();

    // Reset quota counter daily
    if (lastReset !== today) {
      localStorage.setItem('youtube_quota_reset', today);
      localStorage.setItem('youtube_stats_calls', '0');
      localStorage.setItem('youtube_videos_calls', '0');
      setQuotaInfo(prev => ({ ...prev, statsCallsToday: 0, videosCallsToday: 0, estimatedQuotaUsed: 0 }));
    } else {
      const statsCallsToday = parseInt(localStorage.getItem('youtube_stats_calls') || '0');
      const videosCallsToday = parseInt(localStorage.getItem('youtube_videos_calls') || '0');
      setQuotaInfo(prev => ({
        ...prev,
        statsCallsToday,
        videosCallsToday,
        estimatedQuotaUsed: (statsCallsToday * 1) + (videosCallsToday * 3)
      }));
    }

    // Check for cached playlist ID and auto-fetch data
    if (channelId && typeof window !== 'undefined' && window.localStorage) {
      const cacheKey = `youtube_uploads_${channelId}`;
      const cachedPlaylistId = window.localStorage.getItem(cacheKey);

      if (cachedPlaylistId) {
        setUploadsPlaylistId(cachedPlaylistId);

                        // Auto-fetch stats and videos if we have cached playlist ID
        showToast("Auto-loading cached channel data...", "info");
        fetchStatsForChannel(false);

        // Fetch videos after a short delay to avoid hitting API limits
        setTimeout(() => {
          fetchVideosWithPlaylistId(cachedPlaylistId, false);
        }, 1000);
      }
    }
  }, []);

  const updateQuotaCounter = (type: 'stats' | 'videos') => {
    const key = type === 'stats' ? 'youtube_stats_calls' : 'youtube_videos_calls';
    const current = parseInt(localStorage.getItem(key) || '0');
    const newCount = current + 1;
    localStorage.setItem(key, newCount.toString());

    setQuotaInfo(prev => {
      const newStats = type === 'stats' ? newCount : prev.statsCallsToday;
      const newVideos = type === 'videos' ? newCount : prev.videosCallsToday;
      return {
        statsCallsToday: newStats,
        videosCallsToday: newVideos,
        estimatedQuotaUsed: (newStats * 1) + (newVideos * 3)
      };
    });
  };

  const fetchStatsForChannel = async (showSuccessToast: boolean = true) => {
    if (!channelId.trim()) {
      showToast("Please enter a channel ID", "error");
      return;
    }

    setLoadingStats(true);

    try {
      const result = await fetchYouTubeStats(channelId);

      if (result.success && result.stats) {
        setStats(result.stats);
        updateQuotaCounter('stats');
        if (showSuccessToast) {
          showToast("Stats fetched successfully!", "success");
        }
      } else {
        showToast(result.error || "Failed to fetch stats", "error");
        setStats(null);
      }
    } catch (error) {
      logger.error("Failed to fetch YouTube stats:", error);
      showToast("Failed to fetch YouTube stats", "error");
      setStats(null);
    } finally {
      setLoadingStats(false);
    }
  };

  const handleFetchStats = async () => {
    await fetchStatsForChannel(true);
  };

  const handleGetPlaylistId = async () => {
    if (!channelId.trim()) {
      showToast("Please enter a channel ID", "error");
      return;
    }

    try {
      const result = await getUploadsPlaylistId(channelId);

      if (result.success && result.playlistId) {
        setUploadsPlaylistId(result.playlistId);
        showToast("Uploads playlist ID cached!", "success");
      } else {
        showToast(result.error || "Failed to get playlist ID", "error");
      }
    } catch (error) {
      logger.error("Failed to get playlist ID:", error);
      showToast("Failed to get playlist ID", "error");
    }
  };

    const fetchVideosWithPlaylistId = async (playlistId: string, showSuccessToast: boolean = true) => {
    setLoadingVideos(true);

    try {
      const result = await fetchLatestVideos(playlistId);

      if (result.success && result.videos) {
        setLatestVideos(result.videos);
        updateQuotaCounter('videos');
        if (showSuccessToast) {
          showToast("Latest videos fetched successfully!", "success");
        }
      } else {
        showToast(result.error || "Failed to fetch videos", "error");
        setLatestVideos([] as LatestVideo[]);
      }
    } catch (error) {
      console.error("Failed to fetch latest videos:", error);
      showToast("Failed to fetch latest videos", "error");
      setLatestVideos([] as LatestVideo[]);
    } finally {
      setLoadingVideos(false);
    }
  };

  const handleFetchVideos = async () => {
    if (!uploadsPlaylistId) {
      showToast("Please get uploads playlist ID first", "error");
      return;
    }

    await fetchVideosWithPlaylistId(uploadsPlaylistId);
  };

  const handlePostToESP = async () => {
    if (!stats) {
      showToast("No stats to post. Fetch stats first.", "error");
      return;
    }

    setPostingToESP(true);

    try {
      const result = await postYouTubeStatsToESP({
        subscriberCount: stats.subscriberCount,
        viewCount: stats.viewCount,
        videoCount: stats.videoCount,
        title: stats.title,
        latestVideos: latestVideos.map(video => ({
          title: video.title,
          relativeTime: video.relativeTime
        }))
      });

      if (result.success) {
        showToast("Stats posted to ESP32 successfully!", "success");
      } else {
        showToast(result.message, "error");
      }
    } catch (error) {
      console.error("Failed to post stats to ESP:", error);
      showToast("Failed to post stats to ESP", "error");
    } finally {
      setPostingToESP(false);
    }
  };

  const formatNumber = (num: string): string => {
    const number = parseInt(num);
    if (number >= 1000000) {
      return (number / 1000000).toFixed(1) + "M";
    } else if (number >= 1000) {
      return (number / 1000).toFixed(1) + "K";
    }
    return num;
  };

  const getQuotaStatusColor = (used: number): string => {
    if (used < 5000) return "text-green-400";
    if (used < 8000) return "text-yellow-400";
    return "text-red-400";
  };

  return (
    <Container>
      <PageHeading
        title='YouTube Stats (Development)'
        subtitle='Test YouTube API integration with optimized quota usage and ESP32 posting.'
      />

      <div className="space-y-8">
        {/* API Quota Monitor */}
        <ActionCard
          title='API Quota Monitor'
          description='Track daily YouTube API usage to stay within 10,000 units/day limit.'
        >
          <div className='grid grid-cols-1 md:grid-cols-4 gap-4'>
            <div className='bg-slate-800 p-4 rounded-lg text-center'>
              <div className='text-xl font-bold text-blue-400'>{quotaInfo.statsCallsToday}</div>
              <div className='text-sm text-slate-400'>Stats Calls</div>
              <div className='text-xs text-slate-500'>1 unit each</div>
            </div>
            <div className='bg-slate-800 p-4 rounded-lg text-center'>
              <div className='text-xl font-bold text-purple-400'>{quotaInfo.videosCallsToday}</div>
              <div className='text-sm text-slate-400'>Video Calls</div>
              <div className='text-xs text-slate-500'>3 units each</div>
            </div>
            <div className='bg-slate-800 p-4 rounded-lg text-center'>
              <div className={`text-xl font-bold ${getQuotaStatusColor(quotaInfo.estimatedQuotaUsed)}`}>
                {quotaInfo.estimatedQuotaUsed}
              </div>
              <div className='text-sm text-slate-400'>Total Used</div>
              <div className='text-xs text-slate-500'>/ 10,000 daily</div>
            </div>
            <div className='bg-slate-800 p-4 rounded-lg text-center'>
              <div className='text-xl font-bold text-slate-400'>
                {Math.round((quotaInfo.estimatedQuotaUsed / 10000) * 100)}%
              </div>
              <div className='text-sm text-slate-400'>Quota Used</div>
              <div className='text-xs text-slate-500'>Daily limit</div>
            </div>
          </div>
        </ActionCard>

        {/* Channel Configuration */}
        <ActionCard
          title='Channel Configuration'
          description='Enter a YouTube channel ID and get uploads playlist ID (cached).'
        >
          <div className='space-y-4'>
            <div>
              <label
                className='block text-sm font-medium mb-2 text-left text-white'
                htmlFor='channelId'
              >
                YouTube Channel ID
              </label>
              <input
                type='text'
                id='channelId'
                name='channelId'
                value={channelId}
                onChange={(e) => setChannelId((e.target as HTMLInputElement).value)}
                className='w-full p-3 border border-slate-700 rounded-md bg-[var(--input-bg)] text-white placeholder-slate-400'
                placeholder='UCT44w6854K62cSiwA1aiXfw'
              />
              <p className='text-xs text-slate-400 mt-1'>
                Use the channel ID (starts with UC...) not the @username
              </p>
            </div>

            {uploadsPlaylistId && (
              <div>
                <label className='block text-sm font-medium mb-2 text-left text-white'>
                  Uploads Playlist ID (Cached)
                </label>
                <div className='bg-slate-800 p-3 rounded-md text-green-400 font-mono text-sm'>
                  {uploadsPlaylistId}
                </div>
              </div>
            )}

            <div className='flex flex-wrap gap-4'>
              <Button
                onClick={handleGetPlaylistId}
                disabled={!channelId.trim()}
                className='bg-purple-600 hover:bg-purple-700 px-6 py-2'
              >
                Get Playlist ID
              </Button>
            </div>
          </div>
        </ActionCard>

        {/* Data Fetching Controls */}
        <ActionCard
          title='Data Fetching (Optimized Intervals)'
          description='Separate timers: Stats every 90s (1 unit), Videos every 5min (3 units).'
        >
          <div className='space-y-6'>
            {/* Stats Controls */}
            <div className='border border-slate-700 rounded-lg p-4'>
              <h4 className='text-white font-medium mb-3'>Channel Statistics (90s intervals)</h4>
              <div className='flex items-center space-x-4'>
                <Button
                  onClick={handleFetchStats}
                  disabled={loadingStats || !channelId.trim()}
                  className='bg-blue-600 hover:bg-blue-700 px-6 py-2'
                >
                  {loadingStats ? "Fetching..." : "Fetch Stats"}
                </Button>

                <label className='flex items-center space-x-2 text-white'>
                  <input
                    type='checkbox'
                    checked={autoRefreshStats}
                    onChange={(e) => setAutoRefreshStats((e.target as HTMLInputElement).checked)}
                    className='rounded'
                  />
                  <span className='text-sm'>Auto-refresh (90s)</span>
                </label>
              </div>
            </div>

            {/* Videos Controls */}
            <div className='border border-slate-700 rounded-lg p-4'>
              <h4 className='text-white font-medium mb-3'>Latest Videos (5min intervals)</h4>
              <div className='flex items-center space-x-4'>
                <Button
                  onClick={handleFetchVideos}
                  disabled={loadingVideos || !uploadsPlaylistId}
                  className='bg-green-600 hover:bg-green-700 px-6 py-2'
                >
                  {loadingVideos ? "Fetching..." : "Fetch Videos"}
                </Button>

                <label className='flex items-center space-x-2 text-white'>
                  <input
                    type='checkbox'
                    checked={autoRefreshVideos}
                    onChange={(e) => setAutoRefreshVideos((e.target as HTMLInputElement).checked)}
                    className='rounded'
                    disabled={!uploadsPlaylistId}
                  />
                  <span className='text-sm'>Auto-refresh (5min)</span>
                </label>
              </div>
            </div>
          </div>
        </ActionCard>

        {/* Channel Statistics */}
        {stats && (
          <ActionCard
            title='Channel Statistics'
            description='Current YouTube channel stats and ESP32 integration.'
          >
            <div className='space-y-6'>
              {/* Channel Info */}
              <div className='flex items-start space-x-4'>
                {stats.thumbnailUrl && (
                  <img
                    src={stats.thumbnailUrl}
                    alt={stats.title}
                    className='w-16 h-16 rounded-full'
                  />
                )}
                <div>
                  <h3 className='text-lg font-semibold text-white'>{stats.title}</h3>
                  <p className='text-sm text-slate-400 line-clamp-2'>{stats.description}</p>
                </div>
              </div>

              {/* Stats Grid */}
              <div className='grid grid-cols-1 md:grid-cols-3 gap-4'>
                <div className='bg-slate-800 p-4 rounded-lg text-center'>
                  <div className='text-2xl font-bold text-blue-400'>
                    {formatNumber(stats.subscriberCount)}
                  </div>
                  <div className='text-sm text-slate-400'>Subscribers</div>
                  <div className='text-xs text-slate-500 mt-1'>
                    {parseInt(stats.subscriberCount).toLocaleString()}
                  </div>
                </div>

                <div className='bg-slate-800 p-4 rounded-lg text-center'>
                  <div className='text-2xl font-bold text-green-400'>
                    {formatNumber(stats.viewCount)}
                  </div>
                  <div className='text-sm text-slate-400'>Total Views</div>
                  <div className='text-xs text-slate-500 mt-1'>
                    {parseInt(stats.viewCount).toLocaleString()}
                  </div>
                </div>

                <div className='bg-slate-800 p-4 rounded-lg text-center'>
                  <div className='text-2xl font-bold text-purple-400'>
                    {formatNumber(stats.videoCount)}
                  </div>
                  <div className='text-sm text-slate-400'>Videos</div>
                  <div className='text-xs text-slate-500 mt-1'>
                    {parseInt(stats.videoCount).toLocaleString()}
                  </div>
                </div>
              </div>
            </div>
          </ActionCard>
        )}

        {/* Latest Videos */}
        {latestVideos.length > 0 && (
          <ActionCard
            title='Latest Videos'
            description='Most recent 3 videos from the channel.'
          >
            <div className='space-y-3'>
              {latestVideos.map((video, index) => (
                <div key={video.videoId} className='bg-slate-800 p-4 rounded-lg'>
                  <div className='flex items-start space-x-4'>
                    {/* Video Thumbnail */}
                    <div className='flex-shrink-0'>
                      <a
                        href={`https://www.youtube.com/watch?v=${video.videoId}`}
                        target="_blank"
                        rel="noopener noreferrer"
                      >
                        <img
                          src={video.thumbnailUrl}
                          alt={video.title}
                          className='w-32 h-18 object-cover rounded-lg hover:opacity-80 transition-opacity'
                        />
                      </a>
                    </div>

                    {/* Video Info */}
                    <div className='flex-1 min-w-0'>
                      <div className='flex items-center space-x-2 mb-2'>
                        <span className='bg-red-600 text-white text-xs px-2 py-1 rounded'>
                          #{index + 1}
                        </span>
                        <span className='text-slate-400 text-sm'>{video.relativeTime}</span>
                      </div>
                      <a
                        href={`https://www.youtube.com/watch?v=${video.videoId}`}
                        target="_blank"
                        rel="noopener noreferrer"
                        className='text-white font-medium line-clamp-2 hover:text-blue-400 transition-colors cursor-pointer block'
                      >
                        {video.title}
                      </a>
                    </div>
                  </div>
                </div>
              ))}
            </div>
          </ActionCard>
        )}

        {/* ESP32 Integration */}
        {(stats || latestVideos.length > 0) && (
          <ActionCard
            title='ESP32 LCD Display'
            description='Post combined stats and latest videos to /api/youtube endpoint.'
          >
            <div className='flex justify-between items-center'>
              <div>
                <h4 className='text-white font-medium'>Combined Data Payload</h4>
                <p className='text-sm text-slate-400'>
                  Channel stats + latest video titles for LCD display
                </p>
                <div className='text-xs text-slate-500 mt-1'>
                  {stats ? 'Stats ✓' : 'Stats ✗'} | {latestVideos.length > 0 ? `${latestVideos.length} Videos ✓` : 'Videos ✗'}
                </div>
              </div>
              <Button
                onClick={handlePostToESP}
                disabled={postingToESP || !stats}
                className='bg-green-600 hover:bg-green-700 px-6 py-2'
              >
                {postingToESP ? "Posting..." : "Post to ESP32"}
              </Button>
            </div>
          </ActionCard>
        )}
      </div>
    </Container>
  );
}