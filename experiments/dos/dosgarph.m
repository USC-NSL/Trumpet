x=csvread('dosmeasurenomatch.txt');
couldrun = x(:,3)>0;
noloss = x(:,4)<100;
nonotfinish = x(:,5)<2;
d = unique(x(:,1));
clear m; for i = 1:length(d), di = d(i); m(i,:)=[di,min(x(and(x(:,1)==di,and(and(couldrun, noloss),nonotfinish)),2))]; end;
x2=csvread('dosmeasurenomatch1pkt.txt');
couldrun2 = x2(:,3)>0;
noloss2 = x2(:,4)<100;
nonotfinish2 = x2(:,5)<2;
clear m2; for i = 1:length(d), di = d(i); m2(i,:)=[di,min(x2(and(x2(:,1)==di,and(and(couldrun2, noloss2),nonotfinish2)),2))]; end;
m=[0,1;m];
m2=[0,1;m2];


figure;
hold all;
m(m(:,1)==1,:)=[];
m2(m2(:,1)==1,:)=[];
D2(D2(:,1)==1,:)=[];
plot(m2(:,2)*64/1000,100*D2(:,1),'-s', 'LineWidth',2)
plot(m(:,2)*64/1000,100*D2(:,1),'-*', 'LineWidth',2)
xlabel('Threshold (KB)');
ylabel('DoS traffic BW %');
set(findobj(gcf, 'type','axes'),'fontsize',13)
set(findobj(gcf, 'type','axes'),'fontsize',14)
set(findall(gcf,'type','text'),'fontSize',14')
set(gca,'XTick', [0:4])
set(gca,'YTick', [0 30 60 90])
ylim([0 92]);
legend({'1 packet','=threshold'});
set(gcf,'OuterPosition',[300,300,375,360])
