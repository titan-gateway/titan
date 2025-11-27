# Google Analytics 4 Setup for Titan Website

Quick guide to set up Google Analytics 4 tracking for your Docusaurus documentation site.

## Step 1: Create Google Analytics Property

1. **Go to Google Analytics:**
   - Visit: https://analytics.google.com/
   - Sign in with your Google account

2. **Create Account (if needed):**
   - Click "Start measuring"
   - Account name: `Titan Project` (or your preferred name)
   - Check relevant data-sharing settings
   - Click "Next"

3. **Create Property:**
   - Property name: `Titan Documentation`
   - Time zone: Select your timezone
   - Currency: Select your currency
   - Click "Next"

4. **Business Details (optional):**
   - Industry: Technology/Software
   - Business size: Select appropriate size
   - Click "Next"

5. **Business Objectives (optional):**
   - Select "Get baseline reports" or "Examine user behavior"
   - Click "Create"

6. **Accept Terms of Service**

7. **Set up Data Stream:**
   - Platform: **Web**
   - Website URL: `https://JonathanBerhe.github.io` (your GitHub Pages URL)
   - Stream name: `Titan Docs Site`
   - Click "Create stream"

8. **Copy Measurement ID:**
   - You'll see a Measurement ID like: `G-XXXXXXXXXX`
   - **Copy this ID** - you'll need it next

## Step 2: Add to Docusaurus Config

1. **Open your Docusaurus config:**
   ```bash
   cd website
   vim docusaurus.config.ts  # or your editor of choice
   ```

2. **Find the gtag section** and update with your Measurement ID:
   ```typescript
   presets: [
     [
       'classic',
       {
         docs: { /* ... */ },
         blog: { /* ... */ },
         theme: { /* ... */ },
         gtag: {
           trackingID: 'G-XXXXXXXXXX',  // ← Replace with YOUR ID
           anonymizeIP: true,            // Optional: GDPR compliance
         },
       } satisfies Preset.Options,
     ],
   ],
   ```

3. **Save the file**

## Step 3: Test Tracking

1. **Start development server:**
   ```bash
   npm start
   ```

2. **Open your site in browser:**
   - Navigate to: http://localhost:3000

3. **Check Google Analytics:**
   - Go back to Google Analytics
   - Click on "Reports" → "Realtime"
   - You should see yourself as an active user!

   **Note:** It may take 1-2 minutes for data to appear.

## Step 4: Deploy & Verify

1. **Build and deploy your site:**
   ```bash
   npm run build
   npm run deploy  # or push to trigger GitHub Actions
   ```

2. **Visit your live site:**
   - Example: https://JonathanBerhe.github.io/titan

3. **Verify tracking in GA4:**
   - Check "Realtime" reports
   - Wait 24-48 hours for full reports to populate

## What You'll Track

Google Analytics 4 automatically tracks:

- ✅ **Page views** - Which pages users visit
- ✅ **Sessions** - How long users stay
- ✅ **Traffic sources** - Where visitors come from (Google, HN, Reddit)
- ✅ **User location** - Countries and cities
- ✅ **Device types** - Desktop, mobile, tablet
- ✅ **Real-time users** - Live visitor count
- ✅ **Engagement** - Scroll depth, video plays
- ✅ **Events** - Button clicks, downloads (with custom setup)

## Custom Event Tracking (Optional)

Track specific actions like "Download Binary" or "View Benchmark":

```typescript
// In your React component
import useDocusaurusContext from '@docusaurus/useDocusaurusContext';

function BenchmarkButton() {
  const handleClick = () => {
    // Track custom event
    if (window.gtag) {
      window.gtag('event', 'view_benchmark', {
        event_category: 'engagement',
        event_label: 'titan_vs_nginx',
      });
    }
  };

  return <button onClick={handleClick}>View Benchmarks</button>;
}
```

## Connect with Google Search Console

Link GA4 with Search Console to see:
- Which search queries bring traffic
- Your search rankings
- Click-through rates

**Steps:**
1. Go to Google Analytics → Admin
2. Under "Product Links" → Click "Search Console Links"
3. Click "Link" and follow the prompts

## Useful GA4 Reports

Once you have data, check these reports:

**Acquisition:**
- Where users come from (organic search, social media, direct)

**Engagement:**
- Which pages are most popular
- Average engagement time
- Which content keeps users on site

**Retention:**
- Are users coming back?
- Cohort analysis

**Demographics:**
- User age, gender, interests
- Geographic distribution

## Privacy Considerations

If you're concerned about privacy:

1. **Enable IP anonymization** (already in config above)
2. **Add privacy policy** to your site footer
3. **Cookie consent banner** (optional, depends on jurisdiction)
4. **Data retention settings** in GA4 Admin

## Troubleshooting

### Analytics not showing data?

1. **Check Measurement ID is correct** in `docusaurus.config.ts`
2. **Verify site is deployed** (not just localhost)
3. **Check browser console** for errors
4. **Disable ad blockers** when testing
5. **Wait 24-48 hours** for reports to fully populate

### Getting "This property has no data"?

- Make sure the site is live and you've visited it
- Check that your Measurement ID matches the one in GA4
- Verify the gtag config is in the `presets` section, not at root level

## Alternative: Google Tag Manager (Advanced)

For more control, you can use Google Tag Manager instead:

```typescript
// docusaurus.config.js
scripts: [
  {
    src: 'https://www.googletagmanager.com/gtag/js?id=GTM-XXXXXX',
    async: true,
  },
],
```

This allows non-developers to add tracking without code changes.

## Next Steps

Once you have analytics set up:

1. **Set up goals/conversions:**
   - Track "Download Binary" clicks
   - Track "GitHub Star" clicks
   - Track documentation searches

2. **Create custom dashboards:**
   - Weekly traffic summary
   - Top referrers
   - Popular documentation pages

3. **Set up alerts:**
   - Notify when traffic spikes
   - Alert on site errors

4. **Regular reporting:**
   - Weekly: Check traffic trends
   - Monthly: Analyze top content
   - Quarterly: Review goals & conversions

---

**Need Help?**
- [GA4 Documentation](https://support.google.com/analytics/answer/9304153)
- [Docusaurus Analytics Guide](https://docusaurus.io/docs/api/plugins/@docusaurus/plugin-google-gtag)
- Open an issue: https://github.com/JonathanBerhe/titan/issues
