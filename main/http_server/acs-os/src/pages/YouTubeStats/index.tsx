import { useState, useEffect } from "preact/hooks";
import { Container } from "../../components/Container";
import { PageHeading } from "../../components/PageHeading";
import { ActionCard } from "../../components/ActionCard";
import { Button } from "../../components/Button";
import { fetchYouTubeStats, postYouTubeStatsToESP } from "../../utils/api";
import { useToast } from "../../context/ToastContext";

interface YouTubeStats {
  subscriberCount: string;
  viewCount: string;
  videoCount: string;
  title: string;
  description: string;
  thumbnailUrl: string;
}

export function YouTubeStatsPage() {
  const { showToast } = useToast();
  const [channelId, setChannelId] = useState("UCT44w6854K62cSiwA1aiXfw"); // VoskCoin channel ID as default
  const [stats, setStats] = useState<YouTubeStats | null>(null);
  const [loading, setLoading] = useState(false);
  const [postingToESP, setPostingToESP] = useState(false);
  const [autoRefresh, setAutoRefresh] = useState(false);

  // Auto refresh every 30 seconds if enabled
  useEffect(() => {
    if (!autoRefresh || !channelId) return;

    const interval = setInterval(() => {
      handleFetchStats();
    }, 30000);

    return () => clearInterval(interval);
  }, [autoRefresh, channelId]);

  const handleFetchStats = async () => {
    if (!channelId.trim()) {
      showToast("Please enter a channel ID", "error");
      return;
    }

    setLoading(true);

    try {
      const result = await fetchYouTubeStats(channelId);

      if (result.success && result.stats) {
        setStats(result.stats);
        showToast("Stats fetched successfully!", "success");
      } else {
        showToast(result.error || "Failed to fetch stats", "error");
        setStats(null);
      }
    } catch (error) {
      console.error("Failed to fetch YouTube stats:", error);
      showToast("Failed to fetch YouTube stats", "error");
      setStats(null);
    } finally {
      setLoading(false);
    }
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

  return (
    <Container>
      <PageHeading
        title='YouTube Stats (Development)'
        subtitle='Test YouTube API integration and ESP32 posting functionality.'
      />

      <div className="space-y-8">
        <ActionCard
          title='Channel Configuration'
          description='Enter a YouTube channel ID to fetch statistics.'
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

            <div className='flex items-center space-x-4'>
              <Button
                onClick={handleFetchStats}
                disabled={loading || !channelId.trim()}
                className='bg-blue-600 hover:bg-blue-700 px-6 py-2'
              >
                {loading ? "Fetching..." : "Fetch Stats"}
              </Button>

              <label className='flex items-center space-x-2 text-white'>
                <input
                  type='checkbox'
                  checked={autoRefresh}
                  onChange={(e) => setAutoRefresh((e.target as HTMLInputElement).checked)}
                  className='rounded'
                />
                <span className='text-sm'>Auto-refresh (30s)</span>
              </label>
            </div>
          </div>
        </ActionCard>

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

              {/* ESP32 Integration */}
              <div className='border-t border-slate-700 pt-4'>
                <div className='flex justify-between items-center'>
                  <div>
                    <h4 className='text-white font-medium'>ESP32 LCD Display</h4>
                    <p className='text-sm text-slate-400'>
                      Post stats to /api/youtube endpoint for LCD display
                    </p>
                  </div>
                  <Button
                    onClick={handlePostToESP}
                    disabled={postingToESP}
                    className='bg-green-600 hover:bg-green-700 px-6 py-2'
                  >
                    {postingToESP ? "Posting..." : "Post to ESP32"}
                  </Button>
                </div>
              </div>
            </div>
          </ActionCard>
        )}
      </div>
    </Container>
  );
}