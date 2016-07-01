clear;
kk=1;
name='netwide';
events=[4 16 64 256];
for eventsnum=events; 
%for e=[64]; 
    c=csvread(sprintf('%s_%d_c.log',name,eventsnum)); 
    for i=1:4, 
        s{i}=csvread(sprintf('%s_%d_%d.log',name,eventsnum, i-1)); 
    end;
    getnetworkwidestats;
    multirunstats{kk}= stats;
    multirunstatsm{kk}= statsm;
    kk=kk+1;
end

cols = {'1st Sat-Poll', 'Last ask-1st ask', '1st Sat-Last sat','1st Sat-Poll result'};

for i = 1:length(events), 
    meanvalues(i,:) = mean(multirunstats{i}(:,[3,4])); 
    stdvalues(i,:) = std(multirunstats{i}(:,[3,4])); 
end
figure; 
barwitherr(stdvalues(:,2),meanvalues(:,2));
set(gca,'XTickLabel', arrayfun(@num2str, events, 'unif', 0));
ylabel('Time (ms)');
xlabel('# events')
set(findall(gcf,'type','text'),'fontSize',14)
set(findobj(gcf, 'type','axes'),'fontsize',14)
xlim([0.5, 4+0.5])
set(gcf,'OuterPosition',[500,500,375,360])
