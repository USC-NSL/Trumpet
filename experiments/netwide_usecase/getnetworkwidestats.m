% #ask 1
% #satisfaction 2
% #return 3
% #add 4
% #all received 5
%c cols: time, server, message type, server epoch, event, epoch
%function [statsm,t]=getnetworkwidestats(c,s)
clear statsm statse stats ce ct s2 ee t tt

ee = unique(c(:,5));
stats = [];
statsm = [];
statse = [];
start = min(c(c(:,3)==2,1));
for e2 = 1:length(ee),
    e = ee(e2);
    ce = c(c(:,5)==e,:);
    ce(:,1)=(ce(:,1)-start)/2.6e6;
    s2 = s;
    for i=1:length(s)
       s2{i}(:,1)=(s2{i}(:,1)-s2{i}(1,1))/2.3e6;
       s2{i} = s2{i}(s2{i}(:,5)==e,:);
    end
    tt = unique(ce(:,6));
    k=1;
    for i = 1:length(tt) 
        ti = tt(i); 
        ct = ce(ce(:,6)==ti,:);
        if (sum(ct(:,3)==2,1)==0) 
            continue; 
        end
        t{e2}(k)=ti;
        k=k+1;
    end
    k = length(t{e2});
    satnum = zeros(k,1);
    firstsat = zeros(k,1);
    lastsat = zeros(k,1);
    firstask = zeros(k,1);
    resultready = zeros(k,1);
    pollend = zeros(k,1);
    lastask = zeros(k,1);
    for i = 1:k 
        ti = t{e2}(i); 
        ct = ce(ce(:,6)==ti,:);
        satnum(i) = sum(ct(:,3)==2,1);
        if (satnum(i) < 4)
            continue;
        end
        firstsat(i)= min(ct(ct(:,3)==2, 1)); 
        lastsat(i)= max(ct(ct(:,3)==2, 1)); 
        firstask(i)= min(ct(ct(:,3)==1, 1)); 
        lastask(i)= max(ct(ct(:,3)==1, 1)); 
        try
            resultready(i)= min(ct(ct(:,3)==5,1)); %with rwlock, there can be multiple!
            pollend(i)= max(ct(ct(:,3)==3, 1)); 
        catch
            display(sprintf('%d error event %d at %d',i,e,ti));
            satnum(i) =0;
        end
        
    end
    index = satnum == 4;
    stats=[stats; firstask(index)- firstsat(index), lastask(index)- firstsat(index), lastsat(index)- firstsat(index), pollend(index)- firstsat(index)];
    statsm{e2}=[t{e2}(index)', firstsat(index), lastsat(index), pollend(index)];
    t{e2}=t{e2}(index);
end

% figure; hold all; for i=1:size(stats,2),[h, hx]=hist(stats(:,i),1000); 
% plot(hx, cumsum(h)/sum(h), 'LineWidth', 2); end; 
% legend({'1st Sat-Poll', 'Last ask-1st ask', '1st Sat-Last sat','1st Sat-Poll result'});
% set(findall(gcf,'type','text'),'fontSize',14')
% set(findobj(gcf, 'type','axes'),'fontsize',14)
% xlabel('Time(ms)');
% ylabel('CDF');
% 
% 
% figure;
% barwitherr(std(stats), mean(stats))
% ylabel('Time (ms)');
% set(gca,'XTickLabel', {'1st Sat-Poll', 'Last ask-1st ask', '1st Sat-Last sat','1st Sat-Poll result'});
% set(findobj(gcf, 'type','axes'),'fontsize',14)
% set(findall(gcf,'type','text'),'fontSize',14')
% 
% color = jet(101); 
% figure; 
% hold all; 
% mmin=1000; 
% mmax=0; 
% for i=1:length(statsm), 
%     mmin=min(mmin, min(statsm{i}(:,4)-statsm{i}(:,2))); 
%     mmax=max(mmax, max(statsm{i}(:,4)-statsm{i}(:,2))); 
% end
% range = mmax - mmin;
% for i=1:length(statsm), 
%      for j=1:size(statsm{i},1), 
%          col=color(floor((statsm{i}(j,4)-statsm{i}(j,2)-mmin)/range*100+1),:);  
%          plot(statsm{i}(j,1),i,'.', 'Color',col); 
%      end; 
% %     plot(statsm{i}(:,1),i+statsm{i}(:,4)-statsm{i}(:,2)); 
% end
%end